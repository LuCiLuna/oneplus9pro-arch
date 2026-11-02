/*
 * Drives the patched vp9_v4l2m2m decoder the way Firefox does, without Firefox
 * or any GPU work: packets with microsecond timestamps and no timebase, the
 * send/receive loop, and optionally holding the last N frames as a renderer does.
 *
 * dectest <file.ivf> <drm|nv12> <hold frames> <seconds> [full,key,gl]
 *
 * gl: import each frame's planes as EGL images and draw them, as Firefox does.
 * Link needs no EGL libraries: everything is looked up with dlsym at runtime.
 */
#define EGL_EGLEXT_PROTOTYPES_DISABLED
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <dlfcn.h>
#include <pthread.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/pixdesc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int want_drm;

#define GL_FNS \
	F(EGLDisplay, eglGetPlatformDisplay, EGLenum, void *, const EGLAttrib *) \
	F(EGLBoolean, eglInitialize, EGLDisplay, EGLint *, EGLint *) \
	F(EGLBoolean, eglBindAPI, EGLenum) \
	F(EGLBoolean, eglChooseConfig, EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *) \
	F(EGLContext, eglCreateContext, EGLDisplay, EGLConfig, EGLContext, const EGLint *) \
	F(EGLBoolean, eglMakeCurrent, EGLDisplay, EGLSurface, EGLSurface, EGLContext) \
	F(EGLImage, eglCreateImage, EGLDisplay, EGLContext, EGLenum, EGLClientBuffer, const EGLAttrib *) \
	F(EGLBoolean, eglDestroyImage, EGLDisplay, EGLImage) \
	F(EGLint, eglGetError, void) \
	F(void, glGenTextures, GLsizei, GLuint *) \
	F(void, glBindTexture, GLenum, GLuint) \
	F(void, glTexParameteri, GLenum, GLenum, GLint) \
	F(void, glTexImage2D, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *) \
	F(void, glGenFramebuffers, GLsizei, GLuint *) \
	F(void, glBindFramebuffer, GLenum, GLuint) \
	F(void, glFramebufferTexture2D, GLenum, GLenum, GLenum, GLuint, GLint) \
	F(GLuint, glCreateShader, GLenum) \
	F(void, glShaderSource, GLuint, GLsizei, const GLchar *const *, const GLint *) \
	F(void, glCompileShader, GLuint) \
	F(GLuint, glCreateProgram, void) \
	F(void, glAttachShader, GLuint, GLuint) \
	F(void, glLinkProgram, GLuint) \
	F(void, glUseProgram, GLuint) \
	F(GLint, glGetUniformLocation, GLuint, const GLchar *) \
	F(void, glUniform1i, GLint, GLint) \
	F(void, glActiveTexture, GLenum) \
	F(void, glViewport, GLint, GLint, GLsizei, GLsizei) \
	F(void, glVertexAttribPointer, GLuint, GLint, GLenum, GLboolean, GLsizei, const void *) \
	F(void, glEnableVertexAttribArray, GLuint) \
	F(void, glBindAttribLocation, GLuint, GLuint, const GLchar *) \
	F(void, glDrawArrays, GLenum, GLint, GLsizei) \
	F(void, glFinish, void)

#define F(ret, name, ...) static ret (*p_##name)(__VA_ARGS__);
GL_FNS
#undef F
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC p_image_target;
static EGLDisplay dpy;
static GLuint tex[2];
static int gl_ok;
static long long gl_draws, gl_fails;

static void gl_init(void)
{
	static const EGLint cfg_attr[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
	static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	static const char *vs = "attribute vec2 p; varying vec2 v; void main(){v=p*0.5+0.5;gl_Position=vec4(p,0.,1.);}";
	static const char *fs = "precision mediump float; varying vec2 v; uniform sampler2D y; uniform sampler2D uv;"
		"void main(){vec2 c=texture2D(uv,v).rg-0.5; float l=texture2D(y,v).r;"
		"gl_FragColor=vec4(l+1.5*c.y,l-0.2*c.x-0.5*c.y,l+1.8*c.x,1.);}";
	static const GLfloat quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	void *egl = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
	void *gles = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_GLOBAL);
	EGLConfig cfg;
	EGLint n;
	GLuint fbo, target, shader[2], prog;

	if (!egl || !gles) {
		fprintf(stderr, "no EGL/GLES\n");
		return;
	}
	void *(*get_proc)(const char *) = dlsym(RTLD_DEFAULT, "eglGetProcAddress");

	if (!get_proc) {
		fprintf(stderr, "no eglGetProcAddress\n");
		return;
	}
#define F(ret, name, ...) \
	p_##name = dlsym(RTLD_DEFAULT, #name); \
	if (!p_##name) \
		*(void **)&p_##name = get_proc(#name); \
	if (!p_##name) { \
		fprintf(stderr, "missing %s\n", #name); \
		return; \
	}
	GL_FNS
#undef F
	p_image_target = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)get_proc("glEGLImageTargetTexture2DOES");
	if (!p_image_target) {
		fprintf(stderr, "missing glEGLImageTargetTexture2DOES\n");
		return;
	}

	dpy = p_eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	if (!p_eglInitialize(dpy, NULL, NULL)) {
		fprintf(stderr, "eglInitialize failed %#x\n", p_eglGetError());
		return;
	}
	p_eglBindAPI(EGL_OPENGL_ES_API);
	p_eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n);
	p_eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE,
			 p_eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr));

	p_glGenTextures(1, &target);
	p_glBindTexture(GL_TEXTURE_2D, target);
	p_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1440, 3216, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	p_glGenFramebuffers(1, &fbo);
	p_glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
	p_glViewport(0, 0, 1440, 3216);

	shader[0] = p_glCreateShader(GL_VERTEX_SHADER);
	p_glShaderSource(shader[0], 1, &vs, NULL);
	p_glCompileShader(shader[0]);
	shader[1] = p_glCreateShader(GL_FRAGMENT_SHADER);
	p_glShaderSource(shader[1], 1, &fs, NULL);
	p_glCompileShader(shader[1]);
	prog = p_glCreateProgram();
	p_glAttachShader(prog, shader[0]);
	p_glAttachShader(prog, shader[1]);
	p_glBindAttribLocation(prog, 0, "p");
	p_glLinkProgram(prog);
	p_glUseProgram(prog);
	p_glUniform1i(p_glGetUniformLocation(prog, "y"), 0);
	p_glUniform1i(p_glGetUniformLocation(prog, "uv"), 1);
	p_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
	p_glEnableVertexAttribArray(0);

	p_glGenTextures(2, tex);
	for (int i = 0; i < 2; i++) {
		p_glBindTexture(GL_TEXTURE_2D, tex[i]);
		p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}
	gl_ok = 1;
	fprintf(stderr, "gl ready\n");
}

/* Same import Firefox does in DMABufSurfaceYUV: R8 for Y, GR88 for UV */
static void gl_draw(const AVFrame *frame)
{
	const AVDRMFrameDescriptor *d = (const AVDRMFrameDescriptor *)frame->data[0];
	const AVDRMLayerDescriptor *l = &d->layers[0];
	EGLImage img[2];

	for (int i = 0; i < 2; i++) {
		const EGLAttrib a[] = {
			EGL_WIDTH, frame->width >> i, EGL_HEIGHT, frame->height >> i,
			EGL_LINUX_DRM_FOURCC_EXT, i ? 0x38385247 /* GR88 */ : 0x20203852 /* R8 */,
			EGL_DMA_BUF_PLANE0_FD_EXT, d->objects[0].fd,
			EGL_DMA_BUF_PLANE0_OFFSET_EXT, l->planes[i].offset,
			EGL_DMA_BUF_PLANE0_PITCH_EXT, l->planes[i].pitch,
			EGL_NONE,
		};

		img[i] = p_eglCreateImage(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, a);
		if (img[i] == EGL_NO_IMAGE) {
			gl_fails++;
			if (i)
				p_eglDestroyImage(dpy, img[0]);
			return;
		}
		p_glActiveTexture(GL_TEXTURE0 + i);
		p_glBindTexture(GL_TEXTURE_2D, tex[i]);
		p_image_target(GL_TEXTURE_2D, img[i]);
	}
	p_glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	p_glFinish();
	gl_draws++;
	p_eglDestroyImage(dpy, img[0]);
	p_eglDestroyImage(dpy, img[1]);
}

static enum AVPixelFormat choose(AVCodecContext *c, const enum AVPixelFormat *f)
{
	(void)c;
	for (; *f != AV_PIX_FMT_NONE; f++) {
		if (want_drm && *f == AV_PIX_FMT_DRM_PRIME)
			return *f;
		if (!want_drm && *f == AV_PIX_FMT_NV12)
			return *f;
	}
	return AV_PIX_FMT_NONE;
}


/* Releases frames from another thread, as a renderer does */
#define RELQ 64
static AVFrame *relq[RELQ];
static unsigned relq_head, relq_tail;
static int rel_stop;
static pthread_mutex_t rel_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t rel_cond = PTHREAD_COND_INITIALIZER;

static void *rel_thread(void *arg)
{
	(void)arg;
	for (;;) {
		AVFrame *f;

		pthread_mutex_lock(&rel_lock);
		while (relq_head == relq_tail && !rel_stop)
			pthread_cond_wait(&rel_cond, &rel_lock);
		if (relq_head == relq_tail) {
			pthread_mutex_unlock(&rel_lock);
			return NULL;
		}
		f = relq[relq_tail++ % RELQ];
		pthread_mutex_unlock(&rel_lock);
		av_frame_free(&f);
	}
}

static void rel_push(AVFrame *f)
{
	pthread_mutex_lock(&rel_lock);
	relq[relq_head++ % RELQ] = f;
	pthread_cond_signal(&rel_cond);
	pthread_mutex_unlock(&rel_lock);
}

static int64_t now_us(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000000LL + t.tv_nsec / 1000;
}

static uint32_t rd32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

int main(int argc, char **argv)
{
	if (argc < 5) {
		fprintf(stderr, "usage: dectest file.ivf drm|nv12 hold seconds [full]\n");
		return 2;
	}
	want_drm = !strcmp(argv[2], "drm");
	int hold = atoi(argv[3]), secs = atoi(argv[4]);
	int full = argc > 5 && strstr(argv[5], "full");
	int use_key_flag = argc > 5 && strstr(argv[5], "key");
	int use_gl = argc > 5 && strstr(argv[5], "gl");
	int use_thread = argc > 5 && strstr(argv[5], "thread");
	pthread_t rel;

	if (use_thread)
		pthread_create(&rel, NULL, rel_thread, NULL);

	if (use_gl)
		gl_init();

	FILE *f = fopen(argv[1], "rb");
	uint8_t hdr[32], fh[12];

	if (!f || fread(hdr, 1, 32, f) != 32 || memcmp(hdr, "DKIF", 4)) {
		fprintf(stderr, "not an ivf file\n");
		return 1;
	}
	uint32_t den = rd32(hdr + 16), num = rd32(hdr + 20);

	const AVCodec *codec = avcodec_find_decoder_by_name("vp9_v4l2m2m");
	AVCodecContext *c = avcodec_alloc_context3(codec);

	c->get_format = choose;
	c->apply_cropping = 0;
	c->coded_width = c->width = 3840;
	c->coded_height = c->height = 2160;
	if (avcodec_open2(c, codec, NULL) < 0) {
		fprintf(stderr, "open failed\n");
		return 1;
	}

	AVPacket *pkt = av_packet_alloc();
	AVFrame **held = calloc(hold + 1, sizeof(*held));
	AVFrame *frame = av_frame_alloc();
	int64_t start = now_us(), out = 0, in = 0, errs = 0;
	int format = -1, idx = 0;

	for (;;) {
		if (now_us() - start > secs * 1000000LL)
			break;
		if (fread(fh, 1, 12, f) != 12)
			break;

		uint32_t size = rd32(fh);
		uint64_t pts = rd32(fh + 4) | (uint64_t)rd32(fh + 8) << 32;
		int64_t us = pts * 1000000LL * num / den;

		av_new_packet(pkt, size);
		if (fread(pkt->data, 1, size, f) != size)
			break;
		pkt->pts = pkt->dts = us;
		pkt->duration = 16667;
		/* as Firefox does: VP9 key frame = not show_existing, frame_type 0 */
		if (use_key_flag && size && !(pkt->data[0] & 0x0c))
			pkt->flags |= AV_PKT_FLAG_KEY;

		if (!full) {
			int64_t wait = us - (now_us() - start);

			if (wait > 0) {
				struct timespec t = { wait / 1000000, (wait % 1000000) * 1000 };

				nanosleep(&t, NULL);
			}
		}

		int ret = avcodec_send_packet(c, pkt);

		av_packet_unref(pkt);
		if (ret < 0 && ret != AVERROR(EAGAIN)) {
			fprintf(stderr, "send_packet error %d at %lld us\n", ret, (long long)us);
			errs++;
			break;
		}
		in++;

		while (avcodec_receive_frame(c, frame) >= 0) {
			format = frame->format;
			out++;
			if (gl_ok && frame->format == AV_PIX_FMT_DRM_PRIME)
				gl_draw(frame);
			if (hold) {
				if (held[idx]) {
					if (use_thread)
						rel_push(held[idx]);
					else
						av_frame_free(&held[idx]);
					held[idx] = NULL;
				}
				held[idx] = av_frame_clone(frame);
				idx = (idx + 1) % hold;
			}
			av_frame_unref(frame);
		}
	}

	if (use_thread) {
		pthread_mutex_lock(&rel_lock);
		rel_stop = 1;
		pthread_cond_signal(&rel_cond);
		pthread_mutex_unlock(&rel_lock);
		pthread_join(rel, NULL);
	}
	for (int i = 0; i < hold; i++)
		av_frame_free(&held[i]);

	printf("mode=%s format=%s hold=%d packets=%lld frames=%lld errors=%lld wall=%llds gl=%d draws=%lld fails=%lld\n",
	       argv[2], format >= 0 ? av_get_pix_fmt_name(format) : "none", hold, (long long)in,
	       (long long)out, (long long)errs, (long long)((now_us() - start) / 1000000),
	       gl_ok, gl_draws, gl_fails);
	return 0;
}
