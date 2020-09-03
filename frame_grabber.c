#include "frame_grabber.h"

void errno_exit(const char *s)
{
        fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
        exit(EXIT_FAILURE);
}

int xioctl(int fh, int request, void *arg)
{
        int r;

        do {
                r = ioctl(fh, request, arg);
        } while (-1 == r && EINTR == errno);

        return r;
}

int grab_frame(capture_device * in, int timeout_ms, unsigned char * data, unsigned int * data_size, unsigned long * timestamp)
{
        struct v4l2_buffer buf;
        struct v4l2_plane planes[1];
        struct timeval tv;
        int r;
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(in->fd, &fds);
        /* Timeout. */
	if(timeout_ms > 0){
        	tv.tv_sec = timeout_ms/1000;
        	tv.tv_usec = (timeout_ms - (timeout_ms/1000)) * 1000 ;
        	r = select(in->fd + 1, &fds, NULL, NULL, &tv);
		if (-1 == r) {
                	if (EINTR == errno){
                	        return 0 ;
                	}
                	return -1 ;
        	}else if (0 == r) {
                	return 0 ;
        	}
	}

	CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP ;
	buf.length = 1;
	buf.index = in->buffer_index ;
        buf.m.planes = planes;

        if (-1 == xioctl(in->fd, VIDIOC_DQBUF, &buf)) {
                switch (errno) {
                case EAGAIN:
                        return 0;

                case EIO:
                        /* Could ignore EIO, see spec. */

                        /* fall through */

                default:
                        printf("Failed to read from device");
                        return -1 ;
                }
        }

        if(data != NULL){
                memcpy(data, in->buffers[buf.index].start, buf.m.planes[0].length);
                (*data_size) = buf.m.planes[0].length ;
        }
	if(timestamp != NULL){
		(*timestamp) = (buf.timestamp.tv_sec * 1000) + (buf.timestamp.tv_usec / 1000) ;
	}
        if (-1 == xioctl(in->fd, VIDIOC_QBUF, &buf))
                errno_exit("VIDIOC_QBUF");

        assert(buf.index < in->buffer_count);
        in->buffer_index ++ ;
        if(in->buffer_index >= in->buffer_count){
                in->buffer_index = 0 ;
        }
        return 1;
}

void stop_frame_grabber(capture_device * in)
{
        enum v4l2_buf_type type;
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        if (-1 == xioctl(in->fd, VIDIOC_STREAMOFF, &type))
                errno_exit("VIDIOC_STREAMOFF");
}

void start_frame_grabber(capture_device * in)
{
        unsigned int i;
        enum v4l2_buf_type type;
        struct v4l2_plane planes[1];
	for (i = 0; i < in->buffer_count; ++i) {
			struct v4l2_buffer buf ;
			struct v4l2_plane planes[1];
			CLEAR(buf);
               	 	buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
                	buf.memory      = V4L2_MEMORY_MMAP;
                	buf.length = 1;
                	buf.m.planes = planes;
                	buf.index = i;

                        if (-1 == xioctl(in->fd, VIDIOC_QBUF, &buf)){
                                printf("Failed to start capture \n");
				errno_exit("VIDIOC_QBUF");
			}
                        //printf("Succeeded to queue one buffer %d \n", i);
	}
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        if (-1 == xioctl(in->fd, VIDIOC_STREAMON, &type))
            errno_exit("VIDIOC_STREAMON");
}

void uninit_device(capture_device * in)
{
        unsigned int i;
        for (i = 0; i < in->buffer_count; ++i)
                 if (-1 == munmap(in->buffers[i].start, in->buffers[i].length))
                    errno_exit("munmap");
        free(in->buffers);
}


int init_mmap(capture_device * in)
{
        struct v4l2_requestbuffers req;

        CLEAR(req);

        req.count = in->buffer_count;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        req.memory = V4L2_MEMORY_MMAP;

        if (-1 == xioctl(in->fd, VIDIOC_REQBUFS, &req)) {
                if (EINVAL == errno) {
                        fprintf(stderr, "device does not support memory mapping\n");
                        return -1 ;
                } else {
                        errno_exit("VIDIOC_REQBUFS");
                }
        }

        if (req.count < in->buffer_count) {
                fprintf(stderr, "Insufficient buffer memory on device\n");
                return -1 ;
        }

        in->buffers = calloc(req.count, sizeof(struct buffer));
        in->buffer_count = req.count ;
        if (!in->buffers) {
                fprintf(stderr, "Out of memory\n");
                return -1 ;
        }

        for (int n_buffers = 0; n_buffers < req.count; ++n_buffers) {
                struct v4l2_buffer buf;
                struct v4l2_plane planes[1];
                CLEAR(buf);

                buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
                buf.memory      = V4L2_MEMORY_MMAP;
                buf.length = 1;
                buf.m.planes = planes;
                buf.index = n_buffers;

                if (-1 == xioctl(in->fd, VIDIOC_QUERYBUF, &buf))
                        errno_exit("VIDIOC_QUERYBUF");

                in->buffers[n_buffers].length = buf.m.planes[0].length;
                in->buffers[n_buffers].start = 
                        mmap(NULL /* start anywhere */,
                              buf.m.planes[0].length,
                              PROT_READ | PROT_WRITE /* required */,
                              MAP_SHARED /* recommended */,
                              in->fd,  buf.m.planes[0].m.mem_offset);

                if (MAP_FAILED == in->buffers[n_buffers].start)
                        return -1 ;
        }
	printf("Done init %d buffers \n", BUFFER_REQUEST);
	return 0 ;
}

int init_device(capture_device * in)
{
        struct v4l2_capability cap;
        struct v4l2_cropcap cropcap;
        struct v4l2_crop crop;
        struct v4l2_format fmt;
        struct v4l2_fmtdesc fmt_desc ;
        unsigned int min;

        if (-1 == xioctl(in->fd, VIDIOC_QUERYCAP, &cap)) {
                if (EINVAL == errno) {
                        fprintf(stderr, "device is no V4L2 device\n");
                        return -1;
                } else {
                        return -1;
                }
        }

        if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)) {
                fprintf(stderr, "device is no video capture device\n");
                return -1;
        }

        printf("device capabilities 0x%x \n", cap.capabilities);
                if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
                        fprintf(stderr, "device does not support streaming i/o\n");
                        return -1;
                }

        CLEAR(cropcap);

        if(ioctl(in->fd, VIDIOC_ENUM_FMT, &fmt_desc) == 0) {
		printf("{ pixelformat = ''%c%c%c%c'', description = ''%s'' }\n",\
				fmt_desc.pixelformat & 0xFF, (fmt_desc.pixelformat >> 8) & 0xFF, \
				(fmt_desc.pixelformat >> 16) & 0xFF, (fmt_desc.pixelformat >> 24) & 0xFF, \
				fmt_desc.description);
	}

        cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

        if (0 == xioctl(in->fd, VIDIOC_CROPCAP, &cropcap)) {
                crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                crop.c = cropcap.defrect; /* reset to default */

                if (-1 == xioctl(in->fd, VIDIOC_S_CROP, &crop)) {
                        switch (errno) {
                        case EINVAL:
                                /* Cropping not supported. */
                                break;
                        default:
                                /* Errors ignored. */
                                break;
                        }
                }
        } else {
                /* Errors ignored. */
        }


        CLEAR(fmt);

        /*if (-1 == xioctl(fd, VIDIOC_G_FMT, &fmt))
               errno_exit("VIDIOC_G_FMT");*/
        fmt.fmt.pix.width       = 1920; //replace
        fmt.fmt.pix.height      = 1080; //replace


        unsigned int image_size = fmt.fmt.pix.width * fmt.fmt.pix.height * 2 ;
        /*if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
               errno_exit("VIDIOC_S_FMT");*/

        struct v4l2_streamparm parm;

        int ret = xioctl(in->fd, VIDIOC_G_PARM, &parm);
        if( -1 == ret){
                printf("Failed to get stream param \n");
        }else{
		printf("fps is : %d/%d \n", parm.parm.capture.timeperframe.numerator, parm.parm.capture.timeperframe.denominator);
	}

        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

        parm.parm.capture.timeperframe.numerator = 30;
        parm.parm.capture.timeperframe.denominator = 1;

        ret = xioctl(in->fd, VIDIOC_S_PARM, &parm);
        if( -1 == ret){
        	printf("Failed to set framerate \n");
        }
        return init_mmap(in);
}

void close_frame_grabber(capture_device * in)
{
        uninit_device(in);
        if (-1 == close(in->fd))
                errno_exit("close");
        in->fd = -1;
}

int open_frame_grabber(char * dev_name, capture_device * in)
{
        struct stat st;

        if (-1 == stat(dev_name, &st)) {
                fprintf(stderr, "Cannot identify '%s': %d, %s\n",
                         dev_name, errno, strerror(errno));
                return -1 ;
        }

        if (!S_ISCHR(st.st_mode)) {
                fprintf(stderr, "%s is no device\n", dev_name);
                return -1 ;
        }

        int fd = open(dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);

        if (-1 == fd) {
                fprintf(stderr, "Cannot open '%s': %d, %s\n",
                         dev_name, errno, strerror(errno));
                return -1 ;
        }
        in->fd = fd ;
        in->buffer_count = BUFFER_REQUEST ;
        in->buffer_index = 0 ;
        return init_device(in);
}



int test_frame_grabber(unsigned int count)
{

        capture_device in ;
        char * dev_name = "/dev/video0";

        open_frame_grabber(dev_name, &in);
        start_frame_grabber(&in);
        unsigned int frame_count = count ;
        struct timeval start, end, diff;
	gettimeofday(&start,NULL);
        while (frame_count-- > 0) {
                for (;;) {
                        int ret = grab_frame(&in, 200, NULL, NULL, NULL) ; //Max 200ms
                        if(ret > 0){
                                break ;
                        }else if(ret < 0){
                                printf("Read frame failed \n");
                                exit(-1);
                        }
                        /* EAGAIN - continue select loop. */
                }
        }
        gettimeofday(&end,NULL);
	timersub(&end, &start, &diff);
	float time = diff.tv_sec + ((float) diff.tv_usec)/1000000.f;
	float fps = count/time ;
        printf("Captured %d frames in %02ld.%06ld \n", count, diff.tv_sec, diff.tv_usec);
	printf("Fps is %f \n", fps);
        stop_frame_grabber(&in);
        close_frame_grabber(&in);
        fprintf(stderr, "\n");
        return 0;
}

