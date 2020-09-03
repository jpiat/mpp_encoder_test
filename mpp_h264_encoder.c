#include "mpp_h264_encoder.h"


int open_mpp_encoder(mpp_encoder_data * enc_data){

  if(mpp_create(&(enc_data->mpp_ctx), &(enc_data->mpi))){
        printf("Failed to create encoder \n");
        return -1 ;
  }
  if (mpp_init(enc_data->mpp_ctx, MPP_CTX_ENC, MPP_VIDEO_CodingAVC)){
        printf("Failed to initialize encoder \n");
        return -1 ;
  }
  enc_data->buffer_index = 0 ;

}

int init_mpp_encoder(int width, int height, int fps, mpp_encoder_data * enc_data){

  MppEncRcCfg rc_cfg;
  MppEncCodecCfg codec_cfg;

  memset (&rc_cfg, 0, sizeof (rc_cfg));
  memset (&codec_cfg, 0, sizeof (codec_cfg));

  rc_cfg.change = MPP_ENC_RC_CFG_CHANGE_ALL;
  rc_cfg.rc_mode = MPP_ENC_RC_MODE_CBR;

  rc_cfg.quality = MPP_ENC_RC_QUALITY_BEST;

  rc_cfg.fps_in_flex = 0;
  rc_cfg.fps_in_num = fps;
  rc_cfg.fps_in_denorm = 1;
  rc_cfg.fps_out_flex = 0;
  rc_cfg.fps_out_num =  fps;
  rc_cfg.fps_out_denorm = 1;
  rc_cfg.gop = 13 ;
  rc_cfg.skip_cnt = 0;
  rc_cfg.max_reenc_times = 0;

  codec_cfg.h264.qp_init = 26;

  if (rc_cfg.rc_mode == MPP_ENC_RC_MODE_CBR) {
    codec_cfg.h264.qp_max = 28;
    codec_cfg.h264.qp_min = 4;
    codec_cfg.h264.qp_max_step = 8;

    /* Bits of a GOP */
    rc_cfg.bps_target = width
        * height
        / 8 * fps;
    rc_cfg.bps_target /= 2 ;
    rc_cfg.bps_max = rc_cfg.bps_target * 17 / 16;
    rc_cfg.bps_min = rc_cfg.bps_target * 15 / 16;
  } else if (rc_cfg.rc_mode == MPP_ENC_RC_MODE_VBR) {
    if (rc_cfg.quality == MPP_ENC_RC_QUALITY_CQP) {
      codec_cfg.h264.qp_max = 26;
      codec_cfg.h264.qp_min = 26;
      codec_cfg.h264.qp_max_step = 0;

      rc_cfg.bps_target = -1;
      rc_cfg.bps_max = -1;
      rc_cfg.bps_min = -1;
    } else {
      codec_cfg.h264.qp_max = 40;
      codec_cfg.h264.qp_min = 12;
      codec_cfg.h264.qp_max_step = 0;
      codec_cfg.h264.qp_init = 0;

      rc_cfg.bps_target = width
        * height
        / 8 * 30;
      rc_cfg.bps_max = rc_cfg.bps_target * 17 / 16;
      rc_cfg.bps_min = rc_cfg.bps_target * 1 / 16;
    }
  }

  if (enc_data->mpi->control (enc_data->mpp_ctx, MPP_ENC_SET_RC_CFG,
          &rc_cfg)) {
    printf("Setting rate control for rockchip mpp failed \n");
    return -1;
  }

  codec_cfg.coding = MPP_VIDEO_CodingAVC;
  codec_cfg.h264.change = MPP_ENC_H264_CFG_CHANGE_PROFILE | MPP_ENC_H264_CFG_CHANGE_ENTROPY | MPP_ENC_H264_CFG_CHANGE_TRANS_8x8 | MPP_ENC_H264_CFG_CHANGE_QP_LIMIT;
  codec_cfg.h264.profile = 100;
  codec_cfg.h264.level = 40;
  codec_cfg.h264.entropy_coding_mode = 1;
  codec_cfg.h264.cabac_init_idc = 0;
  codec_cfg.h264.transform8x8_mode = 1;

  if (enc_data->mpi->control (enc_data->mpp_ctx,
          MPP_ENC_SET_CODEC_CFG, &codec_cfg)) {
    printf("Setting codec info for rockchip mpp failed \n");
    return -1;
  }

  MppEncPrepCfg prep_cfg;
  MppEncHeaderMode header_mode;

  memset (&prep_cfg, 0, sizeof (prep_cfg));
  prep_cfg.change = MPP_ENC_PREP_CFG_CHANGE_INPUT |
      MPP_ENC_PREP_CFG_CHANGE_FORMAT;
  prep_cfg.width = width;
  prep_cfg.height =  height;
  prep_cfg.format = MPP_FMT_YUV422_YUYV;
  prep_cfg.hor_stride = ceil((double) (width*2.f)/16)*16;
  prep_cfg.ver_stride = ceil((double) height/16.f)*16;

  if (enc_data->mpi->control (enc_data->mpp_ctx, MPP_ENC_SET_PREP_CFG, &prep_cfg)) {
    printf("Setting input format for rockchip mpp failed \n");
    return -1;
  }

  header_mode = MPP_ENC_HEADER_MODE_EACH_IDR;
  if (enc_data->mpi->control (enc_data->mpp_ctx,
          MPP_ENC_SET_HEADER_MODE, &header_mode)) {
    printf("Setting header mode for rockchip mpp failed \n");
    return -1;
  }
  printf("Done configurating encoder \n");

  int frame_size = prep_cfg.hor_stride * prep_cfg.ver_stride ;
  if (mpp_buffer_group_get_internal (&(enc_data->input_group), MPP_BUFFER_TYPE_ION)){
        printf("Failed to get input group \n");
        return -1 ;
  }
  if (mpp_buffer_group_get_internal (&(enc_data->output_group), MPP_BUFFER_TYPE_ION)){
          printf("Failed to get output group \n");
          return -1 ;
  }

  for (int i = 0; i < MPP_MAX_BUFFERS; i++) {
    if(mpp_buffer_get (enc_data->input_group, &(enc_data->input_buffer[i]), frame_size)){
            //Cannot be done at startup ?
            printf("Failed to map buffers \n");
            return -1 ;
    }
  }

  if (mpp_frame_init (&enc_data->mpp_frame)) {
          printf("Failed to initialize frame \n");
          return -1 ;
  }

  mpp_frame_set_width (enc_data->mpp_frame, width);
  mpp_frame_set_height (enc_data->mpp_frame, height);
  mpp_frame_set_hor_stride (enc_data->mpp_frame, ceil((double) (width*2.f)/16)*16);
  mpp_frame_set_ver_stride (enc_data->mpp_frame, ceil((double) height/16.f)*16);
 
  printf("Setting encoder to polling \n");

  if (enc_data->mpi->poll (enc_data->mpp_ctx, MPP_PORT_INPUT, MPP_POLL_BLOCK)){
          printf("Failed to check mpp ready  \n");
  }

  //Folowing seems to have no effect since we are not using tasks
  /*if (enc_data->mpi->control (enc_data->mpp_ctx, MPP_SET_INTPUT_BLOCK_TIMEOUT,  MPP_POLL_NON_BLOCK)) {
    printf("Setting input to non blocking  \n");
    return -1;
  }*/

  /*if (enc_data->mpi->control (enc_data->mpp_ctx, MPP_SET_OUTPUT_TIMEOUT,  MPP_POLL_NON_BLOCK)) {
    printf("Setting output to non blocking  \n");
    return -1;
  }*/

  return 0 ;
}

int close_mpp_encoder(mpp_encoder_data * enc_data){
	for (int i = 0; i < MPP_MAX_BUFFERS; i++) {
		if (enc_data->input_buffer[i]) {
			mpp_buffer_put (enc_data->input_buffer[i]);
			enc_data->input_buffer[i] = NULL;
		}
	}
	mpp_frame_deinit (&enc_data->mpp_frame);
	mpp_buffer_group_put (enc_data->input_group);
        mpp_buffer_group_put (enc_data->output_group);
	mpp_destroy(enc_data->mpp_ctx);
}

int get_mpp_encoder_input_buffer_ptr(mpp_encoder_data * enc_data, unsigned char ** ptr){
	MppBuffer frame_in = enc_data->input_buffer[enc_data->buffer_index] ;
	(*ptr) = mpp_buffer_get_ptr(frame_in);
	if((*ptr) == NULL){
                printf("Failed to get pointer \n");
                return -1 ;
        }
	return 0 ;
}

int push_frame_mpp_encoder(/*unsigned char * frame, unsigned int frame_size, */mpp_encoder_data * enc_data){
  	int i = 0 ;
//	printf("Starting push frame \n");

  	/*if (enc_data->mpi->poll (enc_data->mpp_ctx, MPP_PORT_INPUT, MPP_POLL_NON_BLOCK)){
      		printf("Encoder not available \n");
		return 0 ; //Cannot write to encoder
  	}*/
	//printf("Encoder available \n");
	MppBuffer frame_in = enc_data->input_buffer[enc_data->buffer_index] ;
	mpp_frame_set_buffer(enc_data->mpp_frame, frame_in);
	mpp_frame_set_eos (enc_data->mpp_frame, 0);

	if(enc_data->mpi->encode_put_frame(enc_data->mpp_ctx, enc_data->mpp_frame)){
		printf("Failed to push frame to encoder");
		return -1 ;
	}

	enc_data->buffer_index ++ ; //What happen of we increase while not dequeuing fast enough
	if(enc_data->buffer_index >= MPP_MAX_BUFFERS) enc_data->buffer_index = 0 ;
	return 0 ;
}


int pop_buffer_mpp_encoder(unsigned char * nal, unsigned int * nal_size, mpp_encoder_data * enc_data){
	MppPacket packet = NULL;
  /*if (enc_data->mpi->poll (enc_data->mpp_ctx, MPP_PORT_OUTPUT, MPP_POLL_NON_BLOCK)){
      	printf("No packet available \n");
	return 0 ; //Cannot read from encoder
  }*/
  if (enc_data->mpi->encode_get_packet (enc_data->mpp_ctx, &packet)) {
          return -1 ;
  }
  if (packet) {
    void * ptr  = mpp_packet_get_pos(packet);
    (*nal_size) = mpp_packet_get_length (packet);
    if (mpp_packet_get_eos (packet)){
            printf("Got a EOS packet \n");
            return -1 ;
    }
    memcpy(nal, ptr, (*nal_size));
    mpp_packet_deinit (&packet);
    return 1 ;
  }
  return 0 ;
}


int test_mpp_h264_encoder(int count){
  mpp_encoder_data enc_data ;
	struct timeval start, end, diff ;
	unsigned char * frame, *nal ;
	unsigned int frame_size, nal_size ;
	unsigned char * nal_buffer = malloc(1920*1080*2);
	unsigned char * image_buffer = malloc(1920*1080*2);
	memset(image_buffer, 0, 1920*1080*2);
	open_mpp_encoder( &enc_data);
	init_mpp_encoder(1920, 1080, 30, &enc_data);
	while(count > 0){
		unsigned char * input_buffer ; 
		get_mpp_encoder_input_buffer_ptr(&enc_data, &input_buffer);
		memset(input_buffer, 0, 1920*1080*2);
		int push_ret = push_frame_mpp_encoder(/*image_buffer, (1920*1080*2), */&enc_data) ;
		if(push_ret < 0){
			printf("Push Error");
			exit(-1);
		}
		int pop_ret =  pop_buffer_mpp_encoder(nal_buffer, &nal_size, &enc_data) ;
		if(pop_ret > 0){
			gettimeofday(&end,NULL);
			timersub(&end, &start, &diff);
			printf("%02ld.%06ld : %u : 0x%x \n", diff.tv_sec, diff.tv_usec, nal_size,(unsigned int) nal_buffer[4]);
			count -- ;
			//fflush(stdout);
			gettimeofday(&start,NULL);
		}else if(pop_ret < 0 ){
			printf("Pop Error");
			exit(-1); 
		}

	}
	printf("Done \n");
	close_mpp_encoder(&enc_data);
	return 1 ;
}

