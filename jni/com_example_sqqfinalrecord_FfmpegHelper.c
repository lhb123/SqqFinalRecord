#include <stdio.h>
#include <time.h> 
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
//#include "libswresample/swresample.h"
#include "libavutil/audio_fifo.h"
#include "libavutil/log.h"
#include "com_example_sqqfinalrecord_FfmpegHelper.h"
#include <jni.h>
#include <android/log.h>

#define LOGE(format, ...)  __android_log_print(ANDROID_LOG_ERROR, "sqqlog", format, ##__VA_ARGS__)
#define LOGI(format, ...)  __android_log_print(ANDROID_LOG_INFO,  "sqqlog", format, ##__VA_ARGS__)

AVBitStreamFilterContext* faacbsfc = NULL;
AVFormatContext *ofmt_ctx;
AVCodec* pCodec,*pCodec_a;
AVCodecContext* pCodecCtx,*pCodecCtx_a;
AVStream* video_st,*audio_st;
AVPacket enc_pkt,enc_pkt_a;
AVFrame *pFrameYUV,*pFrame;

char *filedir;
int width = 600;
int height = 800;
int framecnt = 0;
int framecnt_a = 0;
int nb_samples = 0;
int yuv_width;
int yuv_height;
int y_length;
int uv_length;
int64_t start_time;
int aud_pts;
int vid_pts;
int frameSize = 0;

int init_video(){
	//编码器的初始化
	pCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
	if (!pCodec){
		LOGE("Can not find video encoder!\n");
		return -1;
	}
	pCodecCtx = avcodec_alloc_context3(pCodec);
	pCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
	pCodecCtx->width = width;
	pCodecCtx->height = height;
	pCodecCtx->time_base.num = 1;
	pCodecCtx->time_base.den = 30;
	pCodecCtx->bit_rate = 800000;
	pCodecCtx->gop_size = 250;
	/* Some formats want stream headers to be separate. */
	if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
		pCodecCtx->flags |= CODEC_FLAG_GLOBAL_HEADER;
	pCodecCtx->qmin = 10;
	pCodecCtx->qmax = 51;
	//Optional Param
	pCodecCtx->max_b_frames = 3;
	// Set H264 preset and tune
	AVDictionary *param = 0;
	//av_dict_set(&param, "preset", "ultrafast", 0);
	av_dict_set(&param, "preset", "veryfast", 0);
	av_dict_set(&param, "tune", "zerolatency", 0);

	if (avcodec_open2(pCodecCtx, pCodec, &param) < 0){
		LOGE("Failed to open video encoder!\n");
		return -1;
	}

	//Add a new stream to output,should be called by the user before avformat_write_header() for muxing
	video_st = avformat_new_stream(ofmt_ctx, pCodec);
	if (video_st == NULL){
		return -1;
	}
	video_st->time_base.num = 1;
	video_st->time_base.den = 30;
	video_st->codec = pCodecCtx;

	return 0;
}

int init_audio(){
	pCodec_a = avcodec_find_encoder(AV_CODEC_ID_AAC);
	if(!pCodec_a){
		LOGE("Can not find audio encoder!\n");
		return -1;
	}
	pCodecCtx_a = avcodec_alloc_context3(pCodec_a);

	pCodecCtx_a->channels = 2;

	pCodecCtx_a->channel_layout = av_get_default_channel_layout(
			pCodecCtx_a->channels);

	pCodecCtx_a->sample_rate = 44100;//44100 8000
	pCodecCtx_a->sample_fmt = AV_SAMPLE_FMT_S16;
	pCodecCtx_a->bit_rate = 64000;
	pCodecCtx_a->time_base.num = 1;
	pCodecCtx_a->time_base.den = pCodecCtx_a->sample_rate;
	pCodecCtx_a->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
	/* Some formats want stream headers to be separate. */
	if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
		pCodecCtx_a->flags |= CODEC_FLAG_GLOBAL_HEADER;

	if(avcodec_open2(pCodecCtx_a,pCodec_a,NULL)<0){
		LOGE("Failed to open audio encoder!\n");
		return -1;
	}

	audio_st = avformat_new_stream(ofmt_ctx,pCodec_a);
	if(audio_st == NULL){
		return -1;
	}
	audio_st->time_base.num = 1;
	audio_st->time_base.den = pCodecCtx_a->sample_rate;
	audio_st->codec = pCodecCtx_a;

	return 0;
}

/*
 * Class:     com_example_sqqfinalrecord_FfmpegHelper
 * Method:    init
 * Signature: ([B)I
 */
JNIEXPORT jint JNICALL Java_com_example_sqqfinalrecord_FfmpegHelper_init
  (JNIEnv *env, jclass cls, jbyteArray filename /*,jbyteArray path*/){

	filedir = (char*)(*env)->GetByteArrayElements(env, filename, 0);
	//const char* out_path = "rtmp://10.0.3.114:1935/live/demo";
	//const char* out_path = "rtmp://10.0.6.114:1935/live/demo";

	yuv_width=width;
	yuv_height=height;
	y_length=width*height;
	uv_length=width*height/4;

	av_register_all();
	faacbsfc =  av_bitstream_filter_init("aac_adtstoasc");

	//初始化输出格式上下文
	avformat_alloc_output_context2(&ofmt_ctx, NULL, "flv", /*out_path*/filedir);
	if(init_video()!=0){
		return -1;
	}

	if(init_audio()!=0){
		return -1;
	}
	//Open output URL,set before avformat_write_header() for muxing
	if (avio_open(&ofmt_ctx->pb, filedir/*out_path*/, AVIO_FLAG_READ_WRITE) < 0){
		LOGE("Failed to open output file!\n");
		return -1;
	}

	//Write File Header
	avformat_write_header(ofmt_ctx, NULL);

	start_time = av_gettime();

	return 0;
}

/*
 * Class:     com_example_sqqfinalrecord_FfmpegHelper
 * Method:    start
 * Signature: ([B)I
 */
JNIEXPORT jint JNICALL Java_com_example_sqqfinalrecord_FfmpegHelper_start
  (JNIEnv *env, jclass cls, jbyteArray yuv){
	//传递进来yuv数据
	int ret;
	int enc_got_frame=0;
	int i=0;

	pFrameYUV = av_frame_alloc();
	uint8_t *out_buffer = (uint8_t *)av_malloc(avpicture_get_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height));
	avpicture_fill((AVPicture *)pFrameYUV, out_buffer, AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);

	jbyte* in= (jbyte*)(*env)->GetByteArrayElements(env,yuv,0);
	memcpy(pFrameYUV->data[0],in,y_length);
	(*env)->ReleaseByteArrayElements(env,yuv,in,0);
	for(i=0;i<uv_length;i++)
	{
		*(pFrameYUV->data[2]+i)=*(in+y_length+i*2);
		*(pFrameYUV->data[1]+i)=*(in+y_length+i*2+1);
	}

	pFrameYUV->format = AV_PIX_FMT_YUV420P;
	pFrameYUV->width = yuv_width;
	pFrameYUV->height = yuv_height;

	enc_pkt.data = NULL;
	enc_pkt.size = 0;
	av_init_packet(&enc_pkt);

	ret = avcodec_encode_video2(pCodecCtx, &enc_pkt, pFrameYUV, &enc_got_frame);
	av_frame_free(&pFrameYUV);

	if (enc_got_frame == 1){
		LOGI("Succeed to encode video frame: %5d\tsize:%5d\n", framecnt, enc_pkt.size);
		framecnt++;
		enc_pkt.stream_index = video_st->index;

		//Write PTS
		AVRational time_base=ofmt_ctx->streams[0]->time_base;

		//表示一秒30帧
		AVRational r_framerate1 = {30, 1 };
		AVRational time_base_q = AV_TIME_BASE_Q;

		//Duration between 2 frames (us)两帧之间的时间间隔，这里的单位是微秒
		int64_t calc_duration = (double)(AV_TIME_BASE)*(1 / av_q2d(r_framerate1));	//内部时间戳
		//Parameters
		/*vid_pts = framecnt*calc_duration;
		enc_pkt.pts = av_rescale_q(framecnt*calc_duration, time_base_q, time_base);
		enc_pkt.dts=enc_pkt.pts;
		enc_pkt.duration = av_rescale_q(calc_duration, time_base_q, time_base);
		enc_pkt.pos = -1;*/

		int64_t timett = av_gettime();
		int64_t now_time = timett - start_time;
		vid_pts = now_time;
		enc_pkt.pts = av_rescale_q(now_time, time_base_q, time_base);
		enc_pkt.dts=enc_pkt.pts;
		enc_pkt.duration = av_rescale_q(calc_duration, time_base_q, time_base);
		enc_pkt.pos = -1;

		/*int64_t pts_time = av_rescale_q(enc_pkt.pts,time_base,time_base_q);
		int64_t nows_time = av_gettime() - start_time;
		if((pts_time > nows_time) && (vid_pts + pts_time -nows_time)<aud_pts)
			av_usleep(pts_time-nows_time);*/
		/*if(vid_pts<aud_pts){
			av_usleep(aud_pts-vid_pts);
			LOGE("sleep %d",aud_pts-vid_pts);
		}*/

		ret = av_interleaved_write_frame(ofmt_ctx, &enc_pkt);
		av_free_packet(&enc_pkt);

	}
	return 0;
}

/*
 * Class:     com_example_sqqfinalrecord_FfmpegHelper
 * Method:    startAudio
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_com_example_sqqfinalrecord_FfmpegHelper_startAudio
  (JNIEnv *env, jclass cls, jbyteArray au_data,jint datasize){

	//传递进来pcm数据
	int ret;
	int enc_got_frame=0;
	int i=0;

	pFrame = av_frame_alloc();
	pFrame->nb_samples = pCodecCtx_a->frame_size;
	frameSize = pFrame->nb_samples;
	pFrame->format = pCodecCtx_a->sample_fmt;
	pFrame->channel_layout = pCodecCtx_a->channel_layout;
	pFrame->sample_rate = pCodecCtx_a->sample_rate;

	int size = av_samples_get_buffer_size(NULL,pCodecCtx_a->channels,
			pCodecCtx_a->frame_size,pCodecCtx_a->sample_fmt,1);

	uint8_t *frame_buf = (uint8_t *)av_malloc(size*4);
	avcodec_fill_audio_frame(pFrame,pCodecCtx_a->channels,pCodecCtx_a->sample_fmt,(const uint8_t *)frame_buf,size,1);


	jbyte* in= (jbyte*)(*env)->GetByteArrayElements(env,au_data,0);
	if(memcpy(frame_buf,in,datasize)<=0){
		LOGE("Failed to read raw data!");
		return -1;
	}
	pFrame->data[0] = frame_buf;
	(*env)->ReleaseByteArrayElements(env,au_data,in,0);

	enc_pkt_a.data = NULL;
	enc_pkt_a.size = 0;
	av_init_packet(&enc_pkt_a);
	nb_samples += pFrame->nb_samples;

	ret = avcodec_encode_audio2(pCodecCtx_a,&enc_pkt_a,pFrame, &enc_got_frame);
	av_frame_free(&pFrame);

	if (enc_got_frame == 1){
		LOGI("Succeed to encode audio frame: %5d\tsize:%5d\t bufsize:%5d\n ", framecnt_a, enc_pkt_a.size,size);
		framecnt_a++;
		enc_pkt_a.stream_index = audio_st->index;
		av_bitstream_filter_filter(faacbsfc, pCodecCtx_a, NULL, &enc_pkt_a.data, &enc_pkt_a.size, enc_pkt_a.data, enc_pkt_a.size, 0);

		//Write PTS
		AVRational time_base=ofmt_ctx->streams[audio_st->index]->time_base;

		//表示一秒30帧
		AVRational r_framerate1 = {pCodecCtx_a->sample_rate, 1 };
		AVRational time_base_q = AV_TIME_BASE_Q;

		//Duration between 2 frames (us)两帧之间的时间间隔，这里的单位是微秒
		int64_t calc_duration = (double)(AV_TIME_BASE)*(1 / av_q2d(r_framerate1));	//内部时间戳

		//Parameters
		int64_t timett = av_gettime();
		int64_t now_time = timett - start_time;
		enc_pkt_a.pts = av_rescale_q(now_time, time_base_q, time_base);
		//enc_pkt_a.pts = av_rescale_q(nb_samples*calc_duration, time_base_q, time_base);
		enc_pkt_a.dts=enc_pkt_a.pts;
		enc_pkt_a.duration = av_rescale_q(calc_duration, time_base_q, time_base);
		enc_pkt_a.pos = -1;

		//延时
		/*if(now_time<nb_samples*calc_duration){
			av_usleep(nb_samples*calc_duration - now_time);
		}*/
		/*aud_pts = now_time;
		if(aud_pts<vid_pts){
			av_usleep(vid_pts-aud_pts);
			LOGE("sleep %d",vid_pts-aud_pts);
		}*/
		/*aud_pts = nb_samples*calc_duration;
		int64_t pts_time = av_rescale_q(enc_pkt_a.pts,time_base,time_base_q);
		int64_t now_time = av_gettime() - start_time;
		if((pts_time > now_time) && (aud_pts + pts_time -now_time)<vid_pts)
			av_usleep(pts_time-now_time);*/

		ret = av_interleaved_write_frame(ofmt_ctx, &enc_pkt_a);
		av_free_packet(&enc_pkt_a);
	}

	return 0;
}

int flush_encoder(){
	int ret;
	int got_frame;
	AVPacket enc_pkt;
	if (!(ofmt_ctx->streams[0]->codec->codec->capabilities &
		CODEC_CAP_DELAY))
		return 0;
	while (1) {
		enc_pkt.data = NULL;
		enc_pkt.size = 0;
		av_init_packet(&enc_pkt);
		ret = avcodec_encode_video2(ofmt_ctx->streams[0]->codec, &enc_pkt,
			NULL, &got_frame);
		if (ret < 0)
			break;
		if (!got_frame){
			ret = 0;
			break;
		}
		LOGI("Flush Encoder: Succeed to encode 1 frame!\tsize:%5d\n", enc_pkt.size);
		framecnt++;

		//Write PTS
		AVRational time_base = ofmt_ctx->streams[0]->time_base;//{ 1, 1000 };
		AVRational r_framerate1 = { 30, 1 };
		AVRational time_base_q = { 1, AV_TIME_BASE };
		//Duration between 2 frames (us)
		int64_t calc_duration = (double)(AV_TIME_BASE)*(1 / av_q2d(r_framerate1));	//内部时间戳

		//Parameters
		int64_t timett = av_gettime();
		int64_t now_time = timett - start_time;
		enc_pkt.pts = av_rescale_q(now_time, time_base_q, time_base);
		//enc_pkt.pts = av_rescale_q(framecnt*calc_duration, time_base_q, time_base);
		enc_pkt.dts = enc_pkt.pts;
		enc_pkt.duration = av_rescale_q(calc_duration, time_base_q, time_base);
		enc_pkt.pos = -1;

		/* mux encoded frame */
		ret = av_interleaved_write_frame(ofmt_ctx, &enc_pkt);
		if (ret < 0)
			break;
	}
}

int flush_encoder_a(){
	int ret;
	int got_frame;
	AVPacket enc_pkt_a;
	if (!(ofmt_ctx->streams[audio_st->index]->codec->codec->capabilities &
		CODEC_CAP_DELAY))
		return 0;

	while(1){

		enc_pkt_a.data = NULL;
		enc_pkt_a.size = 0;
		av_init_packet(&enc_pkt_a);
		ret = avcodec_encode_audio2(ofmt_ctx->streams[audio_st->index]->codec,&enc_pkt_a,NULL,&got_frame);
		av_frame_free(NULL);
		if(ret<0){
			break;
		}

		if(!got_frame){
			ret = 0;
			break;
		}
		LOGE("Flush Encoder: Succeed to encode 1 frame!\tsize:%5d\n", enc_pkt_a.size);

		nb_samples += frameSize;
		av_bitstream_filter_filter(faacbsfc, ofmt_ctx->streams[audio_st->index]->codec, NULL, &enc_pkt_a.data, &enc_pkt_a.size, enc_pkt_a.data, enc_pkt_a.size, 0);

		//Write PTS
		AVRational time_base=ofmt_ctx->streams[audio_st->index]->time_base;

		//表示一秒30帧
		AVRational r_framerate1 = {pCodecCtx_a->sample_rate, 1 };
		AVRational time_base_q = AV_TIME_BASE_Q;

		//Duration between 2 frames (us)两帧之间的时间间隔，这里的单位是微秒
		int64_t calc_duration = (double)(AV_TIME_BASE)*(1 / av_q2d(r_framerate1));	//内部时间戳

		//Parameters
		int64_t timett = av_gettime();
		int64_t now_time = timett - start_time;
		enc_pkt_a.pts = av_rescale_q(now_time, time_base_q, time_base);
		//enc_pkt_a.pts = av_rescale_q(nb_samples*calc_duration, time_base_q, time_base);
		enc_pkt_a.dts=enc_pkt_a.pts;
		enc_pkt_a.duration = av_rescale_q(calc_duration, time_base_q, time_base);
		enc_pkt_a.pos = -1;

		ret = av_interleaved_write_frame(ofmt_ctx, &enc_pkt_a);
		if (ret < 0)
			break;
	}
	return 1;
}

/*
 * Class:     com_example_sqqfinalrecord_FfmpegHelper
 * Method:    flush
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_com_example_sqqfinalrecord_FfmpegHelper_flush
  (JNIEnv *env, jclass cls){
	flush_encoder();
	flush_encoder_a();

	LOGE("Flush end\n");
	//Write file trailer
	av_write_trailer(ofmt_ctx);
	return 0;
}


/*
 * Class:     com_example_sqqfinalrecord_FfmpegHelper
 * Method:    close
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_com_example_sqqfinalrecord_FfmpegHelper_close
  (JNIEnv *env, jclass cls){
	if (video_st)
		avcodec_close(video_st->codec);
	if (audio_st)
		avcodec_close(audio_st->codec);

	avio_close(ofmt_ctx->pb);
	avformat_free_context(ofmt_ctx);
	return 0;
}
