/* Stream packed RAW10 frames from /dev/video0 to stdout. */
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define WIDTH 2592
#define HEIGHT 1944
#define BUFFERS 4

int main(void)
{
	int fd, i, type;
	struct v4l2_format fmt;
	struct v4l2_requestbuffers req;
	void *start[BUFFERS];
	unsigned int length[BUFFERS];

	fd = open("/dev/video0", O_RDWR);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = WIDTH;
	fmt.fmt.pix.height = HEIGHT;
	fmt.fmt.pix.pixelformat = v4l2_fourcc('p', 'g', 'A', 'A');
	fmt.fmt.pix.field = V4L2_FIELD_NONE;
	if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
		perror("VIDIOC_S_FMT");
		return 1;
	}
	fprintf(stderr, "sizeimage %u bytesperline %u\n",
		fmt.fmt.pix.sizeimage, fmt.fmt.pix.bytesperline);

	memset(&req, 0, sizeof(req));
	req.count = BUFFERS;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
		perror("VIDIOC_REQBUFS");
		return 1;
	}

	for (i = 0; i < BUFFERS; i++) {
		struct v4l2_buffer buf;

		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
			perror("VIDIOC_QUERYBUF");
			return 1;
		}
		length[i] = buf.length;
		start[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
				MAP_SHARED, fd, buf.m.offset);
		if (start[i] == MAP_FAILED) {
			perror("mmap");
			return 1;
		}
		if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
			perror("VIDIOC_QBUF");
			return 1;
		}
	}

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
		perror("VIDIOC_STREAMON");
		return 1;
	}
	fprintf(stderr, "streaming\n");

	for (;;) {
		struct v4l2_buffer buf;
		unsigned int n;

		memset(&buf, 0, sizeof(buf));
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
			if (errno == EINTR)
				continue;
			perror("VIDIOC_DQBUF");
			return 1;
		}
		n = buf.bytesused ? buf.bytesused : fmt.fmt.pix.sizeimage;
		if (fwrite(start[buf.index], 1, n, stdout) != n)
			return 0;
		fflush(stdout);
		if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
			perror("VIDIOC_QBUF");
			return 1;
		}
	}
}
