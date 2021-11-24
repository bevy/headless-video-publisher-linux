#include <opentok.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include "otk_thread.h"
#define NUM_SAMPLES_PER_FRAME 160
static std::atomic<bool> g_is_connected(false);
static otc_publisher *g_publisher = nullptr;
static std::atomic<bool> g_is_publishing(false);

char *video_input = NULL;
char *audio_input = NULL;
char *apikey = NULL;
char *session_id = NULL;
char *token = NULL;

FILE *audio_fp = NULL;
FILE *video_fp = NULL;

#define EXIT_BREAKOUT_STARTED  100
#define EXIT_BREAKOUT_ENDED    101
int exit_status = EXIT_SUCCESS;  /* to returned when we cleanly stop publishing */

struct custom_video_capturer {
  const otc_video_capturer *video_capturer;
  struct otc_video_capturer_callbacks video_capturer_callbacks;
  int width;
  int height;
  otk_thread_t video_capturer_thread;
  std::atomic<bool> video_capturer_thread_exit;
};

struct audio_device {
  otc_audio_device_callbacks audio_device_callbacks;
  otk_thread_t audio_capturer_thread;
  std::atomic<bool> audio_capturer_thread_exit;
};

static otk_thread_func_return_type video_capturer_thread_start_function(void *arg) {
  struct custom_video_capturer *video_capturer = static_cast<struct custom_video_capturer *>(arg);
  if (video_capturer == nullptr) {
    otk_thread_func_return_value;
  }

  uint8_t *buffer = (uint8_t *)malloc(sizeof(uint8_t) * video_capturer->width * video_capturer->height * 4);
  video_fp = fopen(video_input,"r");
  while(g_is_publishing == false)
	  usleep(10);
  while(video_capturer->video_capturer_thread_exit.load() == false) {
    int bytes = fread(buffer,1280*720*3/2,sizeof(uint8_t),video_fp);
    if(bytes > 0){
    	otc_video_frame *otc_frame = otc_video_frame_new(OTC_VIDEO_FRAME_FORMAT_YUV420P, video_capturer->width, video_capturer->height, buffer);
    	otc_video_capturer_provide_frame(video_capturer->video_capturer, 0, otc_frame);
   	 if (otc_frame != nullptr) {
      		otc_video_frame_delete(otc_frame);
    	}
    	usleep(((1000 / 30) * 1000));
    }
    else{
	fseek(video_fp,0,SEEK_SET);
	if(audio_input != NULL)
		fseek(audio_fp,0,SEEK_SET);
    }
    if (g_is_publishing == false) {
        break;
    }
  }

  if (buffer != nullptr) {
    free(buffer);
  }

  otk_thread_func_return_value;
}

static otk_thread_func_return_type audio_capturer_thread_start_function(void *arg) {
  struct audio_device *device = static_cast<struct audio_device *>(arg);
  if (device == nullptr) {
    otk_thread_func_return_value;
  }
  while(g_is_publishing == false)
	  usleep(10);
  audio_fp = fopen(audio_input,"r");
  while (device->audio_capturer_thread_exit.load() == false) {
  	int16_t samples[NUM_SAMPLES_PER_FRAME];
	size_t actual = fread(samples, NUM_SAMPLES_PER_FRAME,sizeof(int16_t),audio_fp);
	if(actual > 0){
		otc_audio_device_write_capture_data(samples, NUM_SAMPLES_PER_FRAME);
		usleep(10000);
	}
	else{
		if(video_input == NULL)
			fseek(audio_fp,0,SEEK_SET);
	}
        if (g_is_publishing == false) {
            break;
        }

  }
  fclose(audio_fp);
  otk_thread_func_return_value;
}

static otc_bool audio_device_destroy_capturer(const otc_audio_device *audio_device,
                                              void *user_data) {
  struct audio_device *device = static_cast<struct audio_device *>(user_data);
  device->audio_capturer_thread_exit = true;
  otk_thread_join(device->audio_capturer_thread);
  return OTC_TRUE;
}
static otc_bool audio_device_start_capturer(const otc_audio_device *audio_device,
                                            void *user_data) {
  struct audio_device *device = static_cast<struct audio_device *>(user_data);
  device->audio_capturer_thread_exit = false;
  if (otk_thread_create(&(device->audio_capturer_thread), &audio_capturer_thread_start_function, (void *)device) != 0) {
    return OTC_FALSE;
  }
  return OTC_TRUE;
}
static otc_bool audio_device_get_capture_settings(const otc_audio_device *audio_device,
                                                  void *user_data,
                                                  struct otc_audio_device_settings *settings) {
  if (settings == nullptr) {
    return OTC_FALSE;
  }

  settings->number_of_channels = 1;
  settings->sampling_rate = 16000;
  return OTC_TRUE;
}
static otc_bool audio_device_get_render_settings(const otc_audio_device *audio_device,
                                                  void *user_data,
                                                  struct otc_audio_device_settings *settings) {
  if (settings == nullptr) {
    return OTC_FALSE;
  }

  settings->number_of_channels = 1;
  settings->sampling_rate = 16000;
  return OTC_TRUE;
}

static otc_bool video_capturer_init(const otc_video_capturer *capturer, void *user_data) {
  struct custom_video_capturer *video_capturer = static_cast<struct custom_video_capturer *>(user_data);
  if (video_capturer == nullptr) {
    return OTC_FALSE;
  }

  video_capturer->video_capturer = capturer;

  return OTC_TRUE;
}

static otc_bool video_capturer_destroy(const otc_video_capturer *capturer, void *user_data) {
  struct custom_video_capturer *video_capturer = static_cast<struct custom_video_capturer *>(user_data);
  if (video_capturer == nullptr) {
    return OTC_FALSE;
  }

  video_capturer->video_capturer_thread_exit = true;
  otk_thread_join(video_capturer->video_capturer_thread);

  return OTC_TRUE;
}

static otc_bool video_capturer_start(const otc_video_capturer *capturer, void *user_data) {
  struct custom_video_capturer *video_capturer = static_cast<struct custom_video_capturer *>(user_data);
  if (video_capturer == nullptr) {
    return OTC_FALSE;
  }

  video_capturer->video_capturer_thread_exit = false;
  if (otk_thread_create(&(video_capturer->video_capturer_thread), &video_capturer_thread_start_function, (void *)video_capturer) != 0) {
    return OTC_FALSE;
  }

  return OTC_TRUE;
}

static otc_bool get_video_capturer_capture_settings(const otc_video_capturer *capturer,
                                                    void *user_data,
                                                    struct otc_video_capturer_settings *settings) {
  struct custom_video_capturer *video_capturer = static_cast<struct custom_video_capturer *>(user_data);
  if (video_capturer == nullptr) {
    return OTC_FALSE;
  }

  settings->format = OTC_VIDEO_FRAME_FORMAT_ARGB32;
  settings->width = video_capturer->width;
  settings->height = video_capturer->height;
  settings->fps = 30;
  settings->mirror_on_local_render = OTC_FALSE;
  settings->expected_delay = 0;

  return OTC_TRUE;
}

static void on_session_connected(otc_session *session, void *user_data) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;

  g_is_connected = true;

  if ((session != nullptr) && (g_publisher != nullptr)) {
    if (otc_session_publish(session, g_publisher) == OTC_SUCCESS) {
      g_is_publishing = true;
      std::cout << std::endl;
      return;
    }
    std::cout << "Could not publish successfully" << std::endl;
    std::cout << std::endl;
  }
}

static void on_session_connection_created(otc_session *session,
                                          void *user_data,
                                          const otc_connection *connection) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
  std::cout << std::endl;
}

static void on_session_connection_dropped(otc_session *session,
                                          void *user_data,
                                          const otc_connection *connection) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
  std::cout << std::endl;
}

static void on_session_stream_received(otc_session *session,
                                       void *user_data,
                                       const otc_stream *stream) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
  std::cout << std::endl;
}

static void on_session_stream_dropped(otc_session *session,
                                      void *user_data,
                                      const otc_stream *stream) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
  std::cout << std::endl;
}

static void on_session_disconnected(otc_session *session, void *user_data) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
  std::cout << std::endl;
}

static void on_session_error(otc_session *session,
                             void *user_data,
                             const char *error_string,
                             enum otc_session_error_code error) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
  std::cout << "Session error. Error : " << "Code:" << error << " string:" << error_string << std::endl;
  std::cout << std::endl;
}


/* DWP 2021-11-21 */
static void on_session_signal_received(otc_session *session,
                                       void *user_data,
                                       const char *type,
                                       const char *signal,
                                       const otc_connection *connection) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
  std::cout << "     type:" << type << std::endl;
  std::cout << "   signal:" << signal << std::endl;
  if (strcmp(type, "breakoutStarted") == 0) {
    std::cout << "Breakouts started, stopping publishing..." << std::endl;
    exit_status = EXIT_BREAKOUT_STARTED;
    g_is_publishing = false;
    return;
  }
  if (strcmp(type, "reroute") == 0) {
    std::cout << "Breakouts ended, stopping publishing..." << std::endl;
    exit_status = EXIT_BREAKOUT_ENDED;
    g_is_publishing = false;
    return;
  }

  std::cout << std::endl;
  return;

}

/* DWP 2021-11-22 vvvv */
static void on_stream_has_audio_changed(otc_session *session, void *user_data, const otc_stream *stream, otc_bool has_audio) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
  std::cout << "    has_audio: " << has_audio << std::endl;
  std::cout << std::endl;
}

static void on_stream_has_video_changed(otc_session *session, void *user_data, const otc_stream *stream, otc_bool has_video) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
  std::cout << "    has_video: " << has_video << std::endl;
  std::cout << std::endl;
}

static void on_stream_video_dimensions_changed(otc_session *session, void *user_data, const otc_stream *stream, int width, int height) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
  std::cout << "    width: " << width << " height: " << height << std::endl;
  std::cout << std::endl;
}

static void on_stream_video_type_changed(otc_session *session, void *user_data, const otc_stream *stream, enum otc_stream_video_type type) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
  std::cout << "    type: " << type << std::endl;
  std::cout << std::endl;
}

static void on_reconnection_started(otc_session *session, void *user_data) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
  std::cout << std::endl;
}

static void on_reconnected(otc_session *session, void *user_data) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
  std::cout << std::endl;
}

static void on_archive_started(otc_session *session, void *user_data, const char *archive_id, const char *name) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
  std::cout << "    archive id: " << archive_id << " name: " << name << std::endl;
  std::cout << std::endl;
}

static void on_archive_stopped(otc_session *session, void *user_data, const char *archive_id) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
  std::cout << "    archive id: " << archive_id << std::endl;
  std::cout << std::endl;
}

static void on_mute_forced(otc_session *session, void *user_data, otc_on_mute_forced_info *mute_info) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
  std::cout << "    active: " << mute_info->active << std::endl;
  std::cout << std::endl;
}


/* DWP 2021-11-22 ^^^^ */

static void on_publisher_stream_created(otc_publisher *publisher,
                                        void *user_data,
                                        const otc_stream *stream) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
  std::cout << std::endl;
}

static void on_publisher_render_frame(otc_publisher *publisher,
                                      void *user_data,
                                      const otc_video_frame *frame) {
}

static void on_publisher_stream_destroyed(otc_publisher *publisher,
                                          void *user_data,
                                          const otc_stream *stream) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
  std::cout << std::endl;
}

static void on_publisher_error(otc_publisher *publisher,
                               void *user_data,
                               const char* error_string,
                               enum otc_publisher_error_code error_code) {
  std::cout << __FUNCTION__ << " callback function" << std::endl;
  std::cout << "Publisher error. Error code: " << error_string << std::endl;
  std::cout << std::endl;
}

static void on_otc_log_message(const char* message) {
  std::cout <<  __FUNCTION__ << ":" << message << std::endl;
  std::cout << std::endl;
}

int main(int argc, char** argv) {
  if (otc_init(nullptr) != OTC_SUCCESS) {
    std::cout << "Could not init OpenTok library" << std::endl;
    std::cout << std::endl;
    return EXIT_FAILURE;
  }
  int opt;
  while ((opt = getopt(argc, argv, "v:a:t:s:k:")) != -1) {
        switch (opt) {
          case 'v':
            video_input = optarg;
            break;
          case 'a':
            audio_input = optarg;
            break;
          case 's':
            session_id = optarg;
            break;
          case 'k':
            apikey = optarg;
            break;
          case 't':
            token = optarg;
            break;
        }
    }
    if(session_id == NULL || token == NULL || apikey == NULL){
	printf("apikey, sessionid, token are mandatory %s, %s, %s\n",session_id,apikey,token);
	exit(-1);
    }
    if(video_input == NULL && audio_input == NULL){
	printf("You must specify atleast one of - video or audio input\n");
	exit(-1);
    }
    printf("using Video:%s and Audio:%s\n",video_input!=NULL ? video_input: "disabled" ,audio_input!=NULL?audio_input: "disabled");
#ifdef CONSOLE_LOGGING
  otc_log_set_logger_callback(on_otc_log_message);
  otc_log_enable(OTC_LOG_LEVEL_ALL);
#endif
  struct audio_device *device = (struct audio_device *)malloc(sizeof(struct audio_device));
  if(audio_input != NULL){
	  device->audio_device_callbacks = {0};
	  device->audio_device_callbacks.user_data = static_cast<void *>(device);
	  device->audio_device_callbacks.destroy_capturer = audio_device_destroy_capturer;
	  device->audio_device_callbacks.start_capturer = audio_device_start_capturer;
	  device->audio_device_callbacks.get_capture_settings = audio_device_get_capture_settings;
	  device->audio_device_callbacks.get_render_settings = audio_device_get_render_settings;
  }

  struct otc_session_callbacks session_callbacks = {0};
  session_callbacks.on_connected = on_session_connected;
  session_callbacks.on_connection_created = on_session_connection_created;
  session_callbacks.on_connection_dropped = on_session_connection_dropped;
  session_callbacks.on_stream_received = on_session_stream_received;
  session_callbacks.on_stream_dropped = on_session_stream_dropped;
  session_callbacks.on_disconnected = on_session_disconnected;
  session_callbacks.on_error = on_session_error;

  /* DWP 2021-11-21 */
  session_callbacks.on_signal_received = on_session_signal_received;

  /* DWP 2021-11-22 */
  session_callbacks.on_stream_has_audio_changed = on_stream_has_audio_changed;
  session_callbacks.on_stream_has_video_changed = on_stream_has_video_changed;
  session_callbacks.on_stream_video_dimensions_changed = on_stream_video_dimensions_changed;
  session_callbacks.on_stream_video_type_changed = on_stream_video_type_changed;
  session_callbacks.on_reconnection_started = on_reconnection_started;
  session_callbacks.on_reconnected = on_reconnected;
  session_callbacks.on_archive_started = on_archive_started;
  session_callbacks.on_archive_stopped = on_archive_stopped;
  session_callbacks.on_mute_forced = on_mute_forced;



  otc_session *session = nullptr;
  session = otc_session_new(apikey, session_id, &session_callbacks);

  if (session == nullptr) {
    std::cout << "Could not create OpenTok session successfully" << std::endl;
    std::cout << std::endl;
    return EXIT_FAILURE;
  }
  struct custom_video_capturer *video_capturer = (struct custom_video_capturer *)malloc(sizeof(struct custom_video_capturer));
  if(video_input != NULL){
	  video_capturer->video_capturer_callbacks = {0};
	  video_capturer->video_capturer_callbacks.user_data = static_cast<void *>(video_capturer);
	  video_capturer->video_capturer_callbacks.init = video_capturer_init;
	  video_capturer->video_capturer_callbacks.destroy = video_capturer_destroy;
	  video_capturer->video_capturer_callbacks.start = video_capturer_start;
	  video_capturer->video_capturer_callbacks.get_capture_settings = get_video_capturer_capture_settings;
	  video_capturer->width = 1280;
	  video_capturer->height = 720;
  }
  struct otc_publisher_callbacks publisher_callbacks = {0};
  publisher_callbacks.on_stream_created = on_publisher_stream_created;
  publisher_callbacks.on_render_frame = on_publisher_render_frame;
  publisher_callbacks.on_stream_destroyed = on_publisher_stream_destroyed;
  publisher_callbacks.on_error = on_publisher_error;
 
  struct otc_publisher_settings * pub_settings = otc_publisher_settings_new();
  if(video_input == NULL){
  	otc_publisher_settings_set_video_track(pub_settings, OTC_FALSE);
  }
  else{
	otc_publisher_settings_set_video_capturer(pub_settings,&video_capturer->video_capturer_callbacks);
  }
  if(audio_input == NULL){
  	otc_publisher_settings_set_audio_track(pub_settings, OTC_FALSE);
  }
  else{
	otc_set_audio_device(&(device->audio_device_callbacks));
  }
  otc_publisher_settings_set_name(pub_settings,"headless-publisher");
  g_publisher = otc_publisher_new_with_settings( &publisher_callbacks,pub_settings);

  if (g_publisher == nullptr) {
    std::cout << "Could not create OpenTok publisher successfully" << std::endl;
    std::cout << std::endl;
    otc_session_delete(session);   
    return EXIT_FAILURE;
  }
  
  otc_session_connect(session, token);

  std::cout << "Waiting 10 seconds for publisher to start." << std::endl;

  usleep(10000 * 1000);

  while(1){
        if (g_is_publishing) {
	    usleep(100*1000);
        } else {
            break;
        }
  }

  std::cout << std::endl;
  std::cout << "MAIN LOOP DONE!" << std::endl;
  std::cout << std::endl;

  if ((session != nullptr) && (g_publisher != nullptr) && g_is_publishing.load()) {
    otc_session_unpublish(session, g_publisher);
  }
  
  if (g_publisher != nullptr) {
    otc_publisher_delete(g_publisher);
  }

  if ((session != nullptr) && g_is_connected.load()) {
    otc_session_disconnect(session);
  }

  if (session != nullptr) {
    otc_session_delete(session);
  }

  otc_destroy();

  return exit_status;
}
