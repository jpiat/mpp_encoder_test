CFLAGS=-O3
LDFLAGS=-lrockchip_mpp -lm -lpthread



default : test_mpp_h264_encoder test_frame_grabber test_mpp_h264_encoder_with_frame_grabber


test_mpp_h264_encoder_with_frame_grabber : mpp_h264_encoder.o test_mpp_h264_encoder_with_frame_grabber.o  frame_grabber.o
	gcc -o $@ $^ $(LDFLAGS)

test_mpp_h264_encoder : mpp_h264_encoder.o test_mpp_h264_encoder.o
	gcc -o $@ $^ $(LDFLAGS)

test_frame_grabber : frame_grabber.o test_frame_grabber.o
	gcc -o $@ $^ $(LDFLAGS)

%.o : %.c
	gcc -c $(CFLAGS) -o $@ $<

clean :
	rm -f *.o test_mpp_h264_encoder test_frame_grabber test_mpp_h264_encoder_with_frame_grabber
