
#include "frame_grabber.h"
#include "mpp_h264_encoder.h"

#define FPS 30
#define FRAME_TIME_MS (1000/FPS)


int main(int argc, char ** argv){


        capture_device in ;
        mpp_encoder_data enc_data ;
	unsigned long timestamp, last_timestamp ;
        char * dev_name = "/dev/video0";
        unsigned char * nal_buffer = malloc(1920*1080*2);
	unsigned char * image_buffer ;
        unsigned int frame_buffer_size = 0 ;
        unsigned int nal_size = 0 ;
        FILE * output_file = NULL ;
	open_mpp_encoder( &enc_data);
	init_mpp_encoder(1920, 1080, FPS, &enc_data);
	printf("Done init encoder \n");
        open_frame_grabber(dev_name, &in);
	printf("Done init frame grabber \n");
	if(argc < 2){
		printf("Need frame count in argument  \n");
		exit(-1);
	}
	if(argc > 2){
		output_file = fopen(argv[2], "wb");
	}
        unsigned int count = atoi(argv[1]) ;
        unsigned int frame_count = count ;
        struct timeval start, end, diff, start_encoder, end_encoder, diff_encoder;
	gettimeofday(&start,NULL);
	unsigned int pipeline_initialized = 0 ;
	start_frame_grabber(&in);
	last_timestamp = 0 ;
	int drop_frame = 0 ;
	int frop_frame_num = 2 ;
	int drop_frame_denum = 1  ;
	int encode_frame = 1 ;
	unsigned long cumulate_timestamp = 0 ;
        get_mpp_encoder_input_buffer_ptr(&enc_data, &image_buffer);
	while (frame_count > 0) {
	    //get_mpp_encoder_input_buffer_ptr(&enc_data, &image_buffer);
            struct timeval start_grab, end_grab, diff_grab ;
	    gettimeofday(&start_grab,NULL);
	    int ret = grab_frame(&in, -1, image_buffer, &frame_buffer_size, &timestamp) ; //Non blocking, problem is that v4l2 driver will block until there is a frame in the buffer when iniialized and take more time when mpp is active (for a reason i don't understand

	    if(ret){
                    gettimeofday(&end_grab,NULL);
                    timersub(&end_grab, &start_grab, &diff_grab);
                    printf("Grab time :  %02ld.%06ld  \n", diff_grab.tv_sec, diff_grab.tv_usec);
            }

	    /*if(ret){
	    	unsigned long  diff_stamp = timestamp - last_timestamp ;
		last_timestamp = timestamp ;
		cumulate_timestamp += diff_stamp;
		//printf("%lu \n", diff_stamp);
		if(cumulate_timestamp < FRAME_TIME_MS){
			ret = 0 ;
		}else{
			cumulate_timestamp -= FRAME_TIME_MS ;
		}
            }*/

	    if(ret > 0){
			unsigned long diff = timestamp - last_timestamp ;
			last_timestamp = timestamp ;
			gettimeofday(&start_encoder,NULL);
			if(pipeline_initialized){
				int pop_ret =  pop_buffer_mpp_encoder(nal_buffer, &nal_size, &enc_data) ;
                        	if(ret < 0){
                            		printf("Read frame failed \n");
                            		exit(-1);
                        	}else if(ret == 0){
					printf("No packet \n");
					encode_frame = 0 ;
				}else{
					encode_frame = 1 ;
					frame_count -- ;
				}
				if(output_file != NULL){
					fwrite(nal_buffer, 1, nal_size, output_file);
				}
			}
			if(encode_frame){
                    		int push_ret = push_frame_mpp_encoder(&enc_data) ;
				get_mpp_encoder_input_buffer_ptr(&enc_data, &image_buffer);
                    		if(push_ret < 0){
                    		    	printf("Push Error");
                    		    	exit(-1);
                    		}else{
					get_mpp_encoder_input_buffer_ptr(&enc_data, &image_buffer);
					gettimeofday(&end_encoder,NULL);
                        	        timersub(&end_encoder, &start_encoder, &diff_encoder);
                        	        printf("Encoder time :  %02ld.%06ld :  %u : 0x%x \n", diff_encoder.tv_sec, diff_encoder.tv_usec, nal_size,(unsigned int) nal_buffer[4]);
					pipeline_initialized = 1  ;
		    		}
			}
             }
        }
	if(output_file != NULL){
		fclose(output_file);
	}
        gettimeofday(&end,NULL);
	timersub(&end, &start, &diff);
	float time = diff.tv_sec + ((float) diff.tv_usec)/1000000.f;
	float fps = count/time ;
        printf("Captured %d frames in %02ld.%06ld \n", count, diff.tv_sec, diff.tv_usec);
	printf("Fps is %f \n", fps);
        close_mpp_encoder(&enc_data);
        stop_frame_grabber(&in);
        close_frame_grabber(&in);
        fprintf(stderr, "\n");
        return 0;
}
