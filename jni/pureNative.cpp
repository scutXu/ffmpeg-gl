#include <android/log.h>
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include<android_native_app_glue.h>
#include<thread>
#include<mutex>
#include<queue>
#include<time.h>
extern "C"
{
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

}
using namespace std;

#define NUM_PACKET_CACHE 20
#define ALIGNMENT 1
#define LOG_TAG "MY_TAG"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__);

clock_t t = 0;


queue<AVPacket> packetQueue;
mutex m;

struct GraphicState
{
	int32_t screenWidth;
	int32_t screenHeight;
	EGLDisplay glDisplay;
	EGLSurface glSurface;
	EGLContext glContext;
	GLuint vs;
	GLuint fs;
	GLuint program;
	GLuint texture;
	GLint vPosAttribLoc;
	GLint texUnifLoc;
} graphicState;
struct VideoState
{
	AVFormatContext * formatCtx;
	AVCodecContext * codecCtx;
	AVStream * videoStream;
	SwsContext * swsContext;
	int videoStreamIndex;
	AVFrame * frame;
	uint8_t * rgbFrameData[4];
	int rgbImageStride[4];
	bool readFinished;
	bool playFinished;
	clock_t lastFrameTime;
	clock_t nextTimeStep;
} videoState;

void readVideo()
{
	static AVPacket tempPacket;
    av_init_packet(&tempPacket);
    tempPacket.data = NULL;
    tempPacket.size = 0;
	while(true) {
		if(packetQueue.size() <= NUM_PACKET_CACHE) {
			if(av_read_frame(videoState.formatCtx,&tempPacket) >= 0) {
				if(tempPacket.stream_index == videoState.videoStreamIndex) {
					m.lock();
					packetQueue.push(tempPacket);
					m.unlock();
				}
			}
			else {
				break;
			}
		}
	}
	videoState.readFinished = true;
}
void openVideo()
{
	videoState.readFinished = false;
	videoState.playFinished = false;
	videoState.lastFrameTime = clock();
	videoState.nextTimeStep = 0;
	av_register_all();

	if(avformat_open_input(&videoState.formatCtx, "/storage/sdcard0/1.mp4", NULL, NULL)) {
		LOGI("open video fail");
	}
    if (avformat_find_stream_info(videoState.formatCtx, NULL) < 0) {
    	LOGI("find stream fail");
    }

    int videoStreamIndex = av_find_best_stream(videoState.formatCtx,AVMEDIA_TYPE_VIDEO,-1,-1,NULL,0);
    if(videoStreamIndex < 0) {
    	LOGI("find video stream fail");
    }
    videoState.videoStreamIndex = videoStreamIndex;
    AVStream * videoStream = videoState.formatCtx->streams[videoStreamIndex];
    videoState.codecCtx = videoStream->codec;
    videoState.videoStream = videoStream;
    AVCodec * codec = avcodec_find_decoder(videoState.codecCtx->codec_id);
    if(!codec) {
    	LOGI("find codec fail");
    }
    if(avcodec_open2(videoState.codecCtx,codec,NULL) < 0) {
    	LOGI("open code fail");
    }

    videoState.frame = av_frame_alloc();
    av_image_alloc(videoState.rgbFrameData,videoState.rgbImageStride,
    		videoState.codecCtx->width,videoState.codecCtx->height,
			AVPixelFormat::AV_PIX_FMT_RGB24,ALIGNMENT);
    videoState.swsContext = sws_getContext(videoState.codecCtx->width, videoState.codecCtx->height, videoState.codecCtx->pix_fmt,
    							  videoState.codecCtx->width, videoState.codecCtx->height, AVPixelFormat::AV_PIX_FMT_RGB24,
                             	  SWS_BILINEAR, NULL, NULL, NULL);
    if(!videoState.swsContext) {
    	LOGI("create sws context fail");
    }

}
void closeVideo()
{
	av_frame_free(&videoState.frame);
	av_freep(&videoState.rgbFrameData[0]);
	sws_freeContext(videoState.swsContext);
}
GLuint createShader(GLenum type,const char * src)
{
	GLuint shader = glCreateShader(type);
	GLint compiled;
	if(!shader) {
		LOGI("create shader fail");
		return 0;
	}
	glShaderSource(shader,1,&src,NULL);
	glCompileShader(shader);
	glGetShaderiv(shader,GL_COMPILE_STATUS,&compiled);
	if(!compiled) {
		LOGI("compile shader fail");
		GLint infoLen = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
		if ( infoLen > 1 )
		{
			char* infoLog = new char[sizeof(char) * infoLen];
			glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
			LOGI("%s",infoLog);
			delete [] infoLog;
		}
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}
void initGL(android_app * app)
{
	EGLint format, numConfigs;
	EGLConfig config;
	const EGLint DISPLAY_ATTRIBS[] = {
	    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
	    EGL_BLUE_SIZE, 5, EGL_GREEN_SIZE, 6, EGL_RED_SIZE, 5,
	    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
	    EGL_NONE
	};

	const EGLint CONTEXT_ATTRIBS[] = {
	    EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE
	};

	graphicState.glDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if(graphicState.glDisplay == EGL_NO_DISPLAY) {
		LOGI("create display fail");
	}

    if(!eglInitialize(graphicState.glDisplay, NULL, NULL)) {
    	LOGI("init display fail");
    }

    if(!eglChooseConfig(graphicState.glDisplay, DISPLAY_ATTRIBS, &config, 1,&numConfigs)) {
    	LOGI("choose config fail");
    }
    if(numConfigs <= 0) {

    }

    if(!eglGetConfigAttrib(graphicState.glDisplay, config,EGL_NATIVE_VISUAL_ID, &format)) {
    	LOGI("get config attribute fail");
    }

    ANativeWindow_setBuffersGeometry(app->window, 0, 0,format);


    graphicState.glSurface = eglCreateWindowSurface(graphicState.glDisplay, config,app->window, NULL);
    if (graphicState.glSurface == EGL_NO_SURFACE) {
    	LOGI("create surface fail");
    }

    graphicState.glContext = eglCreateContext(graphicState.glDisplay, config, NULL, CONTEXT_ATTRIBS);
    if(graphicState.glContext == EGL_NO_CONTEXT) {
    	LOGI("create context fail");
    }

    if(!eglMakeCurrent(graphicState.glDisplay, graphicState.glSurface, graphicState.glSurface, graphicState.glContext) ||
		!eglQuerySurface(graphicState.glDisplay, graphicState.glSurface, EGL_WIDTH, &graphicState.screenWidth) ||
		!eglQuerySurface(graphicState.glDisplay, graphicState.glSurface, EGL_HEIGHT, &graphicState.screenHeight)) {
    	LOGI("query fail");
    }
    //LOGI("%d  %d",graphicState.screenWidth,graphicState.screenHeight);
    char vSource [] = "attribute vec2 vPos;\n"
    				  "varying vec2 uv;"
    				  "void main() {\n"
    				  "		uv = (vPos + vec2(1,1)) / 2.0;"
    				  "		gl_Position = vec4(vPos,0,1);\n"
    				  "}\n";

    char fSource [] = "precision mediump float;\n"
    				  "uniform sampler2D frameTex;\n"
    				  "varying vec2 uv;\n"
    				  "void main() {\n"
    				  "		gl_FragColor = texture2D(frameTex,uv);\n"
    				  "}\n";
    GLuint program;
    graphicState.vs = createShader(GL_VERTEX_SHADER,vSource);
    graphicState.fs = createShader(GL_FRAGMENT_SHADER,fSource);
    program = glCreateProgram();
    glAttachShader(program,graphicState.vs);
    glAttachShader(program,graphicState.fs);
    glLinkProgram(program);
    GLint linked;
    glGetProgramiv(program,GL_LINK_STATUS,&linked);
    if(!linked) {
    	glDeleteProgram(program);
    	LOGI("link fail");
    }
    graphicState.program = program;
    graphicState.vPosAttribLoc = glGetAttribLocation(program,"vPos");
    graphicState.texUnifLoc = glGetUniformLocation(program,"frameTex");

    glPixelStorei(GL_UNPACK_ALIGNMENT, ALIGNMENT);
    glPixelStorei(GL_PACK_ALIGNMENT, ALIGNMENT);

    GLuint texture;
    glGenTextures(1,&texture);
    glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    graphicState.texture = texture;
    glUniform1i(graphicState.texUnifLoc,0);

	glDisable(GL_DEPTH_TEST);
	glClearColor(1, 1, 1, 1);
	glViewport(0, 0, graphicState.screenWidth, graphicState.screenHeight);
}
void shutdownGL()
{
	glDeleteTextures(1,&graphicState.texture);

	glDeleteShader(graphicState.vs);
	glDeleteShader(graphicState.fs);
	glDeleteProgram(graphicState.program);

	eglMakeCurrent(graphicState.glDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,EGL_NO_CONTEXT);
	eglDestroyContext(graphicState.glDisplay, graphicState.glContext);
	eglDestroySurface(graphicState.glDisplay, graphicState.glSurface);
	eglTerminate(graphicState.glDisplay);
}
void updateGL()
{
	static GLfloat vPosition [] = {
			-1,-1,
			1,-1,
			1,1,
			-1,1
	};
	glUseProgram(graphicState.program);
	glVertexAttribPointer(graphicState.vPosAttribLoc,2,GL_FLOAT,false,0,vPosition);
	glEnableVertexAttribArray(graphicState.vPosAttribLoc);
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLE_FAN,0,4);
	eglSwapBuffers(graphicState.glDisplay, graphicState.glSurface);
}
void refreshFrame()
{
	static bool firstRender = true;
	clock_t currentTime = clock();
	if(currentTime - videoState.lastFrameTime > videoState.nextTimeStep) {
		if(videoState.readFinished && packetQueue.empty()) {
			videoState.playFinished = true;
		}
		else {
			AVPacket p;
			bool isLastFrame;
			t = clock();
			m.lock();
			//LOGI("lock time:%d",clock() - t);
			if(!packetQueue.empty()) {
				if(packetQueue.size() == 1) {
					if(!videoState.readFinished) {
						m.unlock();
						return;
					}
					else {
						isLastFrame = true;
					}
				}
				p = packetQueue.front();
				packetQueue.pop();
				m.unlock();
				if(!isLastFrame) {
					videoState.nextTimeStep = (packetQueue.front().pts - p.pts) *
							av_q2d(videoState.videoStream->time_base) * 1000;
				}
				int frameCompleted;

				t = clock();
				if(avcodec_decode_video2(videoState.codecCtx,videoState.frame,&frameCompleted,&p) < 0) {
					LOGI("decode frame fail");
				}
				LOGI("decode time:%d",clock() - t);

				if(!frameCompleted) {
					LOGI("incompleted frame occur");
				}

				t = clock();
				sws_scale(videoState.swsContext,videoState.frame->data,
						videoState.frame->linesize,0,videoState.codecCtx->height,
						videoState.rgbFrameData,videoState.rgbImageStride);
				LOGI("scale time:%d",clock() - t);

				if(firstRender) {
					//firstRender = false;
					t = clock();
					glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,
							videoState.codecCtx->width,videoState.codecCtx->height,0,
							GL_RGB,GL_UNSIGNED_BYTE,
							videoState.rgbFrameData[0]);
					LOGI("fill texture time:%d",clock() - t);
				}
				else {
					/*t = clock();
					int error = glGetError();
					LOGI("before error:%d",error);
					glTexSubImage2D(graphicState.texture,0,0,0,
							videoState.codecCtx->width,videoState.codecCtx->height,
							GL_RGB,GL_UNSIGNED_BYTE,videoState.rgbFrameData[0]);
					error = glGetError();
					LOGI("after error:%d",error);
					LOGI("fill sub texture time:%d",clock() - t);*/
				}

				av_free_packet(&p);
				updateGL();
				videoState.lastFrameTime = currentTime;
			}
			else {
				m.unlock();
			}
		}
	}
}
void cmdEventHandler(struct android_app * app,int32_t cmd)
{
	switch (cmd) {
        case APP_CMD_SAVE_STATE:
            break;
        case APP_CMD_INIT_WINDOW:
            if (app->window != NULL) {
                initGL(app);
            }
            break;
        case APP_CMD_TERM_WINDOW:
            break;
        case APP_CMD_GAINED_FOCUS:
            break;
        case APP_CMD_LOST_FOCUS:
            break;
    }
}
void shutdownApp()
{
	shutdownGL();
	closeVideo();
}
void android_main(android_app * app)
{
	LOGI("process start");
	app_dummy();

	openVideo();
	thread t(readVideo);

	app->onAppCmd = cmdEventHandler;

	int32_t result;
	int32_t events;
	android_poll_source * source;



	while(true) {
		result = ALooper_pollAll(0,
								 NULL,
								 &events,
								 (void**)&source);
		if(result >=0) {
			if(source) {
				source->process(app, source);
			}
			if(app->destroyRequested) {
				shutdownApp();
				return;
			}
		}
		//t1 = clock();
		//LOGI("outer loop time:%d",t1 - t2);
		refreshFrame();
		//t2 = clock();
		//LOGI("refresh time:%d",t2 - t1);
		if(videoState.playFinished) {
			shutdownApp();
			return;
		}
	}
}
