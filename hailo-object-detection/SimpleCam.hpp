// https :  // github.com/erasta/libcamera-opencv/blob/main/main.cpp
#ifndef SIMPLECAM_HPP
#define SIMPLECAM_HPP

#include <libcamera/libcamera.h>

#include <chrono>
#include <cstdlib>
#include <iomanip>

// Rest of the code...

#include <functional>
#include <iostream>
#include <memory>
#include <opencv2/opencv.hpp>
#include <thread>

#include "MappedBuffer.hpp"
#include "ThreadPool.hpp"
#include "detection_inference.hpp"

#define TIMEOUT_SEC 3

using namespace libcamera;

using Buffer = std::vector<unsigned char>;
using FrameCallback = std::function<void(const Buffer &, const std::vector<HailoDetectionPtr> &detections)>;
using CVMiddleware = std::function<void(cv::Mat &)>;

class SimpleCam {
 private:
  ThreadPool pool;
  DetectionInference inference;

  FrameCallback frameReadyCallback;
  CVMiddleware cvMiddlewareCallback;
  int height = 1080;
  int width = 1920;
  // int height = 720;
  // int width = 1280;
  // int height = 480;
  // int width = 640;
  int quality = 50;
  std::atomic<bool> cameraRolling{false};
  std::atomic<bool> started{false};

  void matCallback(cv::Mat mat, std::vector<HailoDetectionPtr> &detections);

 public:
  void processRequest(Request *request);
  void requestComplete(Request *request);
  std::string cameraName(Camera *camera);
  void convertToJpeg(const cv::Mat &frame, std::vector<unsigned char> &buffer, const int quality);

  void bufferComplete(Request *request, FrameBuffer *frameBuffer);

  void setFrameCallback(FrameCallback callback) {
    frameReadyCallback = std::move(callback);
  }
  void setCVMiddlewareCallback(CVMiddleware callback) {
    cvMiddlewareCallback = std::move(callback);
  }

  int start();
  int go();
  int finish();

  std::shared_ptr<Camera> camera;
  std::unique_ptr<std::thread> aThread;
  Stream *stream;
  FrameBufferAllocator *allocator;
  std::unique_ptr<CameraManager> cm;
  std::vector<std::unique_ptr<Request>> requests;

  SimpleCam() {
    pool.setupThreads(4);
    inference.setMatCallback(
        [this](cv::Mat mat, std::vector<HailoDetectionPtr> &detections) { this->matCallback(mat, detections); });
  }

  ~SimpleCam() {}
};

/*
 * --------------------------------------------------------------------
 * Handle RequestComplete
 *
 * For each Camera::requestCompleted Signal emitted from the Camera the
 * connected Slot is invoked.
 *
 * The Slot is invoked in the CameraManager's thread, hence one should avoid
 * any heavy processing here. The processing of the request shall be re-directed
 * to the application's thread instead, so as not to block the CameraManager's
 * thread for large amount of time.
 *
 * The Slot receives the Request as a parameter.
 */

void SimpleCam::convertToJpeg(const cv::Mat &frame, std::vector<unsigned char> &buffer, const int quality = 90) {
  // Set JPEG compression parameters
  std::vector<int> compression_params;
  compression_params.push_back(cv::IMWRITE_JPEG_QUALITY);
  compression_params.push_back(quality);  // Set JPEG quality (0 - 100), 90 is a good quality setting
  compression_params.push_back(cv::IMWRITE_JPEG_OPTIMIZE);
  compression_params.push_back(1);

  // Encode image to JPEG format in memory
  if (!cv::imencode(".jpg", frame, buffer, compression_params)) {
    std::cout << "Error: Could not encode the image to JPEG format." << std::endl;
    return;
  }
}

void SimpleCam::matCallback(cv::Mat mat, std::vector<HailoDetectionPtr> &detections) {
  pool.enqueue([mat, detections, this]() {
    Buffer jpgBuffer;
    convertToJpeg(mat, jpgBuffer, quality);

    if (frameReadyCallback) {
      frameReadyCallback(jpgBuffer, detections);
    }
  });
}

void SimpleCam::bufferComplete(Request *request, FrameBuffer *frameBuffer) {
  MappedFrameBuffer mappedBuffer(frameBuffer, MappedFrameBuffer::MapFlag::Read);
  const std::vector<libcamera::Span<uint8_t>> &mem = mappedBuffer.planes();

  cv::Mat image(height, width, CV_8UC4, (uint8_t *)(mem[0].data()));

  cv::Mat bgrImage;
  cv::cvtColor(image, bgrImage, cv::COLOR_BGRA2BGR);

  // flip upside down
  cv::flip(bgrImage, bgrImage, -1);

  pool.enqueue([bgrImage, this]() { inference.writeFrame(bgrImage); });
  //   Buffer jpgBuffer;
  //   convertToJpeg(bgrImage, jpgBuffer, quality);

  //   if (frameReadyCallback) {
  //     frameReadyCallback(jpgBuffer);
  //   }
  // });
}

void SimpleCam::requestComplete(Request *request) {
  if (request->status() == Request::RequestCancelled || !cameraRolling) {
    return;
  }
  // std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // std::cout << "Completed " << (void *)request << std::endl;
  // // loop.callLater(std::bind(&processRequest, request));
  // //     // processRequest(request);
  // // }

  // // void SimpleCam::processRequest(Request *request)
  // // {
  // const Request::BufferMap &buffers = request->buffers();
  // // std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // for (auto bufferPair : buffers) {
  //   const Stream *stream = bufferPair.first;
  //   FrameBuffer *buffer = bufferPair.second;
  //   const FrameMetadata &metadata = buffer->metadata();

  //   /* Print some information about the buffer which has completed. */
  //   std::cout << " seq: " << std::setw(6) << std::setfill('0') << metadata.sequence << " bytesused: ";

  //   unsigned int nplane = 0;
  //   for (const FrameMetadata::Plane &plane : metadata.planes()) {
  //     std::cout << plane.bytesused;
  //     if (++nplane < metadata.planes().size())
  //       std::cout << "/";
  //   }

  //   // std::cout << std::endl;

  //   /*
  //    * Image data can be accessed here, but the FrameBuffer
  //    * must be mapped by the application
  //    */

  //   auto cfg = stream->configuration();
  //   unsigned int width = cfg.size.width;
  //   unsigned int height = cfg.size.height;
  //   unsigned int stride = cfg.stride;

  //   std::cout << " size " << width << "x" << height << " stride " << stride << " format " <<
  //   cfg.pixelFormat.toString()
  //             << " sec " << (double)clock() / CLOCKS_PER_SEC << std::endl;

  //   libcamera::MappedFrameBuffer mappedBuffer(buffer, MappedFrameBuffer::MapFlag::Read);
  //   const std::vector<libcamera::Span<uint8_t>> &mem = mappedBuffer.planes();
  //   cv::Mat image(height, width, CV_8UC4, (uint8_t *)(mem[0].data()));
  //   // static int i = 0;

  //   cv::Mat bgrImage;
  //   cv::cvtColor(image, bgrImage, cv::COLOR_BGRA2BGR);

  //   std::vector<unsigned char> jpgBuffer;
  //   convertToJpeg(bgrImage, jpgBuffer, 100);

  // if (frameReadyCallback) {
  //   frameReadyCallback(jpgBuffer);
  // }

  //   cv::imwrite("images/img" + std::to_string((double)clock() / CLOCKS_PER_SEC) + ".jpg", bgrImage);
  // }

  /* Re-queue the Request to the camera. */
  request->reuse(Request::ReuseBuffers);
  camera->queueRequest(request);
}

/*
 * ----------------------------------------------------------------------------
 * Camera Naming.
 *
 * Applications are responsible for deciding how to name cameras, and present
 * that information to the users. Every camera has a unique identifier, though
 * this string is not designed to be friendly for a human reader.
 *
 * To support human consumable names, libcamera provides camera properties
 * that allow an application to determine a naming scheme based on its needs.
 *
 * In this example, we focus on the location property, but also detail the
 * model string for external cameras, as this is more likely to be visible
 * information to the user of an externally connected device.
 *
 * The unique camera ID is appended for informative purposes.
 */
std::string SimpleCam::cameraName(Camera *camera) {
  const ControlList &props = camera->properties();
  std::string name;

  switch (static_cast<bool>(props.get(properties::Location))) {
    case properties::CameraLocationFront:

      name = "Internal front camera";
      break;
    case properties::CameraLocationBack:
      name = "Internal back camera";
      break;
    case properties::CameraLocationExternal:
      name = "External camera";
      // if (props.contains(properties::Model))
      //   name += " '" + props.get(properties::Model) + "'";
      break;
  }

  name += " (" + camera->id() + ")";

  return name;
}
void h(Request *r) {
  std::cout << "C\n";
};

int SimpleCam::start() {
  if (started) {
    return EXIT_SUCCESS;
  }
  started = true;
  /*
   * --------------------------------------------------------------------
   * Create a Camera Manager.
   *
   * The Camera Manager is responsible for enumerating all the Camera
   * in the system, by associating Pipeline Handlers with media entities
   * registered in the system.
   *
   * The CameraManager provides a list of available Cameras that
   * applications can operate on.
   *
   * When the CameraManager is no longer to be used, it should be deleted.
   * We use a unique_ptr here to manage the lifetime automatically during
   * the scope of this function.
   *
   * There can only be a single CameraManager constructed within any
   * process space.
   */
  cm = std::make_unique<CameraManager>();
  cm->start();

  /*
   * Just as a test, generate names of the Cameras registered in the
   * system, and list them.
   */
  for (auto const &camera : cm->cameras()) {
    std::cout << " - " << cameraName(camera.get()) << std::endl;
  }

  /*
   * --------------------------------------------------------------------
   * Camera
   *
   * Camera are entities created by pipeline handlers, inspecting the
   * entities registered in the system and reported to applications
   * by the CameraManager.
   *
   * In general terms, a Camera corresponds to a single image source
   * available in the system, such as an image sensor.
   *
   * Application lock usage of Camera by 'acquiring' them.
   * Once done with it, application shall similarly 'release' the Camera.
   *
   * As an example, use the first available camera in the system after
   * making sure that at least one camera is available.
   *
   * Cameras can be obtained by their ID or their index, to demonstrate
   * this, the following code gets the ID of the first camera; then gets
   * the camera associated with that ID (which is of course the same as
   * cm->cameras()[0]).
   */
  if (cm->cameras().empty()) {
    std::cout << "No cameras were identified on the system." << std::endl;
    cm->stop();
    return EXIT_FAILURE;
  }

  std::string cameraId = cm->cameras()[0]->id();
  camera = cm->get(cameraId);
  camera->acquire();

  /*
   * Stream
   *
   * Each Camera supports a variable number of Stream. A Stream is
   * produced by processing data produced by an image source, usually
   * by an ISP.
   *
   *   +-------------------------------------------------------+
   *   | Camera                                                |
   *   |                +-----------+                          |
   *   | +--------+     |           |------> [  Main output  ] |
   *   | | Image  |     |           |                          |
   *   | |        |---->|    ISP    |------> [   Viewfinder  ] |
   *   | | Source |     |           |                          |
   *   | +--------+     |           |------> [ Still Capture ] |
   *   |                +-----------+                          |
   *   +-------------------------------------------------------+
   *
   * The number and capabilities of the Stream in a Camera are
   * a platform dependent property, and it's the pipeline handler
   * implementation that has the responsibility of correctly
   * report them.
   */

  /*
   * --------------------------------------------------------------------
   * Camera Configuration.
   *
   * Camera configuration is tricky! It boils down to assign resources
   * of the system (such as DMA engines, scalers, format converters) to
   * the different image streams an application has requested.
   *
   * Depending on the system characteristics, some combinations of
   * sizes, formats and stream usages might or might not be possible.
   *
   * A Camera produces a CameraConfigration based on a set of intended
   * roles for each Stream the application requires.
   */
  std::unique_ptr<CameraConfiguration> config = camera->generateConfiguration({StreamRole::Viewfinder});

  /*
   * The CameraConfiguration contains a StreamConfiguration instance
   * for each StreamRole requested by the application, provided
   * the Camera can support all of them.
   *
   * Each StreamConfiguration has default size and format, assigned
   * by the Camera depending on the Role the application has requested.
   */
  StreamConfiguration &streamConfig = config->at(0);
  std::cout << "Default viewfinder configuration is: " << streamConfig.toString() << std::endl;

  /*
   * Each StreamConfiguration parameter which is part of a
   * CameraConfiguration can be independently modified by the
   * application.
   *
   * In order to validate the modified parameter, the CameraConfiguration
   * should be validated -before- the CameraConfiguration gets applied
   * to the Camera.
   *
   * The CameraConfiguration validation process adjusts each
   * StreamConfiguration to a valid value.
   */

  /*
   * The Camera configuration procedure fails with invalid parameters.
   */
  // #if 0
  streamConfig.size.width = width;
  streamConfig.size.height = height;
  // streamConfig.stream->
  streamConfig.pixelFormat = libcamera::formats::XRGB8888;
  streamConfig.bufferCount = 1;

  config->validate();

  int retconfig = camera->configure(config.get());
  if (retconfig) {
    std::cout << "CONFIGURATION FAILED!" << std::endl;
    return EXIT_FAILURE;
  }
  // #endif

  /*
   * Validating a CameraConfiguration -before- applying it will adjust it
   * to a valid configuration which is as close as possible to the one
   * requested.
   */
  config->validate();
  std::cout << "Validated viewfinder configuration is: " << streamConfig.toString() << std::endl;

  /*
   * Once we have a validated configuration, we can apply it to the
   * Camera.
   */
  camera->configure(config.get());

  auto cp = camera->properties();
  auto cc = camera->controls();
  std::cout << "controls:\n";
  for (auto &c : cc) {
    std::cout << c.first->name() << ": " << c.second.toString() << " = " << c.second.def().toString() << std::endl;
    // for (auto &v : c.second.values())
    // {
    //     std::cout << v.toString() << std::endl;
    // }
  }
  std::cout << "properies:\n";
  for (auto &c : cp) {
    std::cout << c.first << ": " << c.second.toString() << std::endl;
  }
  // exit(0);

  /*
   * --------------------------------------------------------------------
   * Buffer Allocation
   *
   * Now that a camera has been configured, it knows all about its
   * Streams sizes and formats. The captured images need to be stored in
   * framebuffers which can either be provided by the application to the
   * library, or allocated in the Camera and exposed to the application
   * by libcamera.
   *
   * An application may decide to allocate framebuffers from elsewhere,
   * for example in memory allocated by the display driver that will
   * render the captured frames. The application will provide them to
   * libcamera by constructing FrameBuffer instances to capture images
   * directly into.
   *
   * Alternatively libcamera can help the application by exporting
   * buffers allocated in the Camera using a FrameBufferAllocator
   * instance and referencing a configured Camera to determine the
   * appropriate buffer size and types to create.
   */
  allocator = new FrameBufferAllocator(camera);

  for (StreamConfiguration &cfg : *config) {
    int ret = allocator->allocate(cfg.stream());
    if (ret < 0) {
      std::cerr << "Can't allocate buffers" << std::endl;
      return EXIT_FAILURE;
    }

    size_t allocated = allocator->buffers(cfg.stream()).size();
    std::cout << "Allocated " << allocated << " buffers for stream" << std::endl;
  }

  /*
   * --------------------------------------------------------------------
   * Frame Capture
   *
   * libcamera frames capture model is based on the 'Request' concept.
   * For each frame a Request has to be queued to the Camera.
   *
   * A Request refers to (at least one) Stream for which a Buffer that
   * will be filled with image data shall be added to the Request.
   *
   * A Request is associated with a list of Controls, which are tunable
   * parameters (similar to v4l2_controls) that have to be applied to
   * the image.
   *
   * Once a request completes, all its buffers will contain image data
   * that applications can access and for each of them a list of metadata
   * properties that reports the capture parameters applied to the image.
   */
  stream = streamConfig.stream();
  const std::vector<std::unique_ptr<FrameBuffer>> &buffers = allocator->buffers(stream);
  for (unsigned int i = 0; i < buffers.size(); ++i) {
    std::unique_ptr<Request> request = camera->createRequest();
    if (!request) {
      std::cerr << "Can't create request" << std::endl;
      return EXIT_FAILURE;
    }

    const std::unique_ptr<FrameBuffer> &buffer = buffers[i];
    for (auto &plane : buffer->planes()) {
      std::cout << "buffer " << i << " length " << plane.length << " at " << plane.offset << std::endl;
    }

    int ret = request->addBuffer(stream, buffer.get());
    if (ret < 0) {
      std::cerr << "Can't set buffer for request" << std::endl;
      return EXIT_FAILURE;
    }

    /*
     * Controls can be added to a request on a per frame basis.
     */
    // ControlList &controls = request->controls();
    // controls.set(controls::Brightness, (float)i / (buffers.size() - 1) * 2 - 1); //0.5);
    // std::cout << "request " << i << " brightness=" << controls.get(controls::Brightness) << std::endl;
    // request->controls().set(controls::AnalogueGain, 100000);   //(float)i / (buffers.size() - 1) * 2 - 1); // 0.5);
    // request->controls().set(controls::ExposureTime, 100000);   //(float)i / (buffers.size() - 1) * 2 - 1); // 0.5);
    // request->controls().set(controls::ExposureValue, 100000);  //(float)i / (buffers.size() - 1) * 2 - 1); // 0.5);
    // request->controls().set(controls::ColourGains, 16);       //(float)i / (buffers.size() - 1) * 2 - 1); // 0.5);
    request->controls().set(controls::AeEnable, true);
    request->controls().set(controls::AwbEnable, true);
    request->controls().set(controls::Brightness, 0.0);
    request->controls().set(controls::Contrast, 1.0);
    request->controls().set(controls::ExposureTime, 0);  // 0 means auto
    request->controls().set(controls::AnalogueGain, 1.0);

    requests.push_back(std::move(request));

    pool.enqueue([this]() { this->inference.startDetection(1080, 1920); });
  }

  /*
   * --------------------------------------------------------------------
   * Signal&Slots
   *
   * libcamera uses a Signal&Slot based system to connect events to
   * callback operations meant to handle them, inspired by the QT graphic
   * toolkit.
   *
   * Signals are events 'emitted' by a class instance.
   * Slots are callbacks that can be 'connected' to a Signal.
   *
   * A Camera exposes Signals, to report the completion of a Request and
   * the completion of a Buffer part of a Request to support partial
   * Request completions.
   *
   * In order to receive the notification for request completions,
   * applications shall connecte a Slot to the Camera 'requestCompleted'
   * Signal before the camera is started.
   */
  camera->requestCompleted.connect(this, &SimpleCam::requestComplete);
  camera->bufferCompleted.connect(this, &SimpleCam::bufferComplete);
  /*
   * --------------------------------------------------------------------
   * Start Capture
   *
   * In order to capture frames the Camera has to be started and
   * Request queued to it. Enough Request to fill the Camera pipeline
   * depth have to be queued before the Camera start delivering frames.
   *
   * For each delivered frame, the Slot connected to the
   * Camera::requestCompleted Signal is called.
   */
  camera->start();
  return EXIT_SUCCESS;
}

// std::unique_ptr<Request> request;
int SimpleCam::go() {
  if (cameraRolling) {
    return EXIT_SUCCESS;
  }
  cameraRolling = true;
  // const std::vector<std::unique_ptr<FrameBuffer>> &buffers = allocator->buffers(stream);
  // // int i = 0;
  // for (unsigned int i = 0; i < buffers.size(); ++i)
  // {
  //     std::unique_ptr<Request> request = camera->createRequest();
  //     if (!request)
  //     {
  //         std::cerr << "Can't create request" << std::endl;
  //         return EXIT_FAILURE;
  //     }

  //     const std::unique_ptr<FrameBuffer> &buffer = buffers[i];
  //     for (auto &plane : buffer->planes())
  //     {
  //         std::cout << "buffer " << i << " length " << plane.length << " at " << plane.offset << std::endl;
  //     }

  //     int ret = request->addBuffer(stream, buffer.get());
  //     if (ret < 0)
  //     {
  //         std::cerr << "Can't set buffer for request" << std::endl;
  //         return EXIT_FAILURE;
  //     }

  //     /*
  //      * Controls can be added to a request on a per frame basis.
  //      */
  //     // ControlList &controls = request->controls();
  //     // controls.set(controls::Brightness, (float)i / (buffers.size() - 1) * 2 - 1); //0.5);
  //     // std::cout << "request " << i << " brightness=" << controls.get(controls::Brightness) << std::endl;
  //     request->controls().set(controls::AnalogueGain, 100000);  //(float)i / (buffers.size() - 1) * 2 - 1); // 0.5);
  //     request->controls().set(controls::ExposureTime, 100000);  //(float)i / (buffers.size() - 1) * 2 - 1); // 0.5);
  //     request->controls().set(controls::ExposureValue, 100000); //(float)i / (buffers.size() - 1) * 2 - 1); // 0.5);
  //     // request->controls().set(controls::ColourGains, 16);       //(float)i / (buffers.size() - 1) * 2 - 1); //
  //     0.5);

  //     requests.push_back(std::move(request));
  // }

  for (std::unique_ptr<Request> &request : requests) {
    std::cout << "Queued " << (void *)request.get() << std::endl;
    camera->queueRequest(request.get());
  }

  /*
   * --------------------------------------------------------------------
   * Run an EventLoop
   *
   * In order to dispatch events received from the video devices, such
   * as buffer completions, an event loop has to be run.
   */
  // loop.timeout(20); // TIMEOUT_SEC);
  // int ret = loop.exec();
  // std::cout << "Capture ran for " << TIMEOUT_SEC << " seconds and "
  //           << "stopped with exit status: " << ret << std::endl;
  return EXIT_SUCCESS;
}

int SimpleCam::finish() {
  if (!cameraRolling) {
    return EXIT_SUCCESS;
  }
  cameraRolling = false;
  started = false;

  this->inference.stop();

  /*
   * --------------------------------------------------------------------
   * Clean Up
   *
   * Stop the Camera, release resources and stop the CameraManager.
   * libcamera has now released all resources it owned.
   */
  // camera->stop();
  // allocator->free(stream);
  // delete allocator;
  // camera->release();
  // camera.reset();
  // cm->stop();

  // std::cout << "clean up complete" << std::endl;

  camera->stop();

  // Disconnect signal connections
  camera->requestCompleted.disconnect(this, &SimpleCam::requestComplete);
  camera->bufferCompleted.disconnect(this, &SimpleCam::bufferComplete);

  allocator->free(stream);
  delete allocator;
  allocator = nullptr;

  camera->release();
  camera.reset();

  // Clear the requests vector
  requests.clear();

  // Reset the stream pointer
  stream = nullptr;

  // Stop and reset the CameraManager
  cm->stop();
  cm.reset();

  std::cout << "clean up complete" << std::endl;

  return EXIT_SUCCESS;
}
#endif  // SIMPLECAM_HPP