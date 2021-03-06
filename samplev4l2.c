#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <linux/videodev2.h>

#include <jpeglib.h>

//
// Sample V4L2
// 
// Limitations:
// 1. Access /dev/video0
// 2. Support device with V4L2_CAP_VIDEO_CAPTURE capability only 
// 3. Support MMAP type video buffer only
// 4. Read one video frame only
//
// Steps:
// 1. Open video device /dev/video0
// 2. Query capability. Supprot video capture device only
// 3. Query crop (resolution) capability of video capture device
// 4. Query video format of video capture device
// 5. Request video buffer
// 6. Query and mmap buffer created by ioctl VIDIOC_REQBUFS
// 7. Queue video buffer and get one frame
// 8. Turn on video streaming
// 9. Dequeue video buffer
// 10.Process video buffer (write jpeg file)
// 11.Turn off video streaming
// 12.Unmap buffer
// 13.Close device
//
// How to build (Ubuntu 14.04 LTS)
// $ sudo apt-get install libjpeg-dev
// $ git clone https://github.com/ArtisteHsu/SampleV4L2.git
// $ cd SampleV4L2
// $ gcc samplev4l2.c -ljpeg -o samplev4l2

void dumpCapabilities(struct v4l2_capability cap);
void dumpCropCapabilities(struct v4l2_cropcap cropcap);
void dumpFormat(struct v4l2_format format);
void jpegWrite();

int main() {
	char *dev_name = "/dev/video0";
	int fd;
	struct v4l2_capability cap;
	struct v4l2_cropcap cropcap;
	struct v4l2_format format;
	struct v4l2_requestbuffers reqBuffers;
	struct v4l2_buffer buffer;
	void *bufferStart;
	unsigned int bufferLength;
	enum v4l2_buf_type type;
	int ret;
 
	// Step 1. Open video device /dev/video0
	fd = open(dev_name, O_RDWR | O_NONBLOCK, 0);
	if (fd < 0) {
		printf("Error: cannot open %s (EINTR=%d)\n", dev_name, EINTR);
		return fd;
	} else {
		printf("%s is open(fd=%d)\n", dev_name, fd);
	}

	// Step 2. Query capability. Supprot video capture device only
	ret = ioctl(fd, VIDIOC_QUERYCAP, &cap);
	if (ret < 0) {
		printf("Error: cannot get VIDIOC_QUERYCAP capability (%d)\n", ret);
		goto V4L2_CLOSE;
	}
	dumpCapabilities(cap);

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		printf("Error: support device with V4L2_CAP_VIDEO_CAPTURE capability only\n");
		goto V4L2_CLOSE;
	}

	// Step 3. Query crop (resolution) capability of video capture device
	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl(fd, VIDIOC_CROPCAP, &cropcap);
	if (ret < 0) {
		printf("Error: cannot get VIDIOC_CROPCAP capability (%d)\n", ret);
		goto V4L2_CLOSE;
	}
	dumpCropCapabilities(cropcap);
	
	// Step 4. Query video format of video capture device
	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl(fd, VIDIOC_G_FMT, &format);
	if (ret < 0) {
		printf("Error: cannot get VIDIOC_G_FMT format (%d)\n", ret);
		goto V4L2_CLOSE;
	}
	dumpFormat(format);

	// Step 5. Request video buffer. MMAP type only
	reqBuffers.count = 1;  // Request 1 for one frame capture
	reqBuffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	reqBuffers.memory = V4L2_MEMORY_MMAP;
	ret = ioctl(fd, VIDIOC_REQBUFS, &reqBuffers);
	if (ret < 0) {
		printf("Error: request video buffer fails (%d)\n", ret);
		goto V4L2_CLOSE;
	}
	printf("V4L2 video buffer count is %d\n", reqBuffers.count);

	// Step 6. Query and mmap buffer created by ioctl VIDIOC_REQBUFS
	buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buffer.memory = V4L2_MEMORY_MMAP;
	buffer.index = 0;	// 1st buffer
	ret = ioctl(fd, VIDIOC_QUERYBUF, &buffer);
	if (ret < 0) {
		printf("Error: query video buffer fails (%d)\n", ret);
		goto V4L2_CLOSE;
	}
	printf("V4L2 video buffer length is %d\n", buffer.length);

	bufferLength = buffer.length;
	bufferStart = mmap(NULL, bufferLength, PROT_READ | PROT_WRITE, 
			MAP_SHARED, fd, buffer.m.offset);
	if (bufferStart == MAP_FAILED) {
		printf("Error: query video buffer fails (%d)\n", ret);
		goto V4L2_CLOSE;
	}

	// Step 7. Queue video buffer and get one frame
	buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buffer.memory = V4L2_MEMORY_MMAP;
	buffer.index = 0;	// 1st buffer
	ret = ioctl(fd, VIDIOC_QBUF, &buffer);
	if (ret < 0) {
		printf("Error: queue video buffer fails (%d)\n", ret);
		goto V4L2_UNMAP;
	}

	// Step 8. Turn on video streaming
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl(fd, VIDIOC_STREAMON, &type);
	if (ret < 0) {
		printf("Error: Stream ON fails (%d)\n", ret);
		goto V4L2_UNMAP;
	}

	// Step 9. Dequeue video buffer
V4L2_DEQUEUE:
	sleep(1); // Sleep 1 second for video buffer ready
	buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buffer.memory = V4L2_MEMORY_MMAP;
	ret = ioctl(fd, VIDIOC_DQBUF, &buffer);
	if (ret < 0) {
		if (errno == EAGAIN) {
			printf("De-queue video buffer gain\n");
			goto V4L2_DEQUEUE;
		}
		printf("Error: de-queue video buffer fails (%d)\n", ret);
		goto V4L2_UNMAP;
	}

	// Step 10. Process video buffer
	jpegWrite((char*)bufferStart, format.fmt.pix.width, format.fmt.pix.height,
			format.fmt.pix.pixelformat, format.fmt.pix.bytesperline);

	// Step 11.Turn off video streaming
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl(fd, VIDIOC_STREAMOFF, &type);
	if (ret < 0) {
		printf("Error: Stream OFF fails (%d)\n", ret);
		goto V4L2_UNMAP;
	}
	// Step 12.Unmap buffer
V4L2_UNMAP:
	ret = munmap(bufferStart, bufferLength);
	if (ret < 0) {
		printf("Error: unmap video buffer fails (%d)\n", ret);
	}
V4L2_CLOSE:
	// Step 13.Close device
	close(fd);
	printf("%s is closed(fd=%d)\n", dev_name, fd);
	return ret;
}


void dumpCapabilities(struct v4l2_capability cap) {
	printf("V4L2 device capability:\n");
	printf("    driver: %s\n", cap.driver);
	printf("    card: %s\n", cap.card);
	printf("    bus_info: %s\n", cap.bus_info);
	printf("    version: 0x%X\n", cap.version);
	printf("    capabilities: 0x%X\n", cap.capabilities);
	if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
		printf("        V4L2_CAP_VIDEO_CAPTURE (0x%08X)\n", V4L2_CAP_VIDEO_CAPTURE);
	}
	if (cap.capabilities & V4L2_CAP_VIDEO_OUTPUT) {
		printf("        V4L2_CAP_VIDEO_OUTPUT (0x%08X)\n", V4L2_CAP_VIDEO_OUTPUT);
	}
	if (cap.capabilities & V4L2_CAP_VIDEO_OVERLAY) {
		printf("        V4L2_CAP_VIDEO_OVERLAY (0x%08X)\n", V4L2_CAP_VIDEO_OVERLAY);
	}
	if (cap.capabilities & V4L2_CAP_VBI_CAPTURE) {
		printf("        V4L2_CAP_VBI_CAPTURE (0x%08X)\n", V4L2_CAP_VBI_CAPTURE);
	}
	if (cap.capabilities & V4L2_CAP_VBI_OUTPUT) {
		printf("        V4L2_CAP_VBI_OUTPUT (0x%08X)\n", V4L2_CAP_VBI_OUTPUT);
	}
	if (cap.capabilities & V4L2_CAP_SLICED_VBI_CAPTURE) {
		printf("        V4L2_CAP_SLICED_VBI_CAPTURE (0x%08X)\n", V4L2_CAP_SLICED_VBI_CAPTURE);
	}
	if (cap.capabilities & V4L2_CAP_SLICED_VBI_OUTPUT) {
		printf("        V4L2_CAP_SLICED_VBI_OUTPUT (0x%08X)\n", V4L2_CAP_SLICED_VBI_OUTPUT);
	}
	if (cap.capabilities & V4L2_CAP_RDS_CAPTURE) {
		printf("        V4L2_CAP_RDS_CAPTURE (0x%08X)\n", V4L2_CAP_RDS_CAPTURE);
	}
	if (cap.capabilities & V4L2_CAP_VIDEO_OUTPUT_OVERLAY) {
		printf("        V4L2_CAP_VIDEO_OUTPUT_OVERLAY (0x%08X)\n", V4L2_CAP_VIDEO_OUTPUT_OVERLAY);
	}
	if (cap.capabilities & V4L2_CAP_HW_FREQ_SEEK) {
		printf("        V4L2_CAP_HW_FREQ_SEEK (0x%08X)\n", V4L2_CAP_HW_FREQ_SEEK);
	}
	if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
		printf("        V4L2_CAP_VIDEO_CAPTURE_MPLANE (0x%08X)\n", V4L2_CAP_VIDEO_CAPTURE_MPLANE);
	}
	if (cap.capabilities & V4L2_CAP_VIDEO_OUTPUT_MPLANE) {
		printf("        V4L2_CAP_VIDEO_OUTPUT_MPLANE (0x%08X)\n", V4L2_CAP_VIDEO_OUTPUT_MPLANE);
	}
	if (cap.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE) {
		printf("        V4L2_CAP_VIDEO_M2M_MPLANE (0x%08X)\n", V4L2_CAP_VIDEO_M2M_MPLANE);
	}
	if (cap.capabilities & V4L2_CAP_TUNER) {
		printf("        V4L2_CAP_TUNER (0x%08X)\n", V4L2_CAP_TUNER);
	}
	if (cap.capabilities & V4L2_CAP_AUDIO) {
		printf("        V4L2_CAP_AUDIO (0x%08X)\n", V4L2_CAP_AUDIO);
	}
	if (cap.capabilities & V4L2_CAP_RADIO) {
		printf("        V4L2_CAP_RADIO (0x%08X)\n", V4L2_CAP_RADIO);
	}
	if (cap.capabilities & V4L2_CAP_MODULATOR) {
		printf("        V4L2_CAP_MODULATOR (0x%08X)\n", V4L2_CAP_MODULATOR);
	}
	if (cap.capabilities & V4L2_CAP_READWRITE) {
		printf("        V4L2_CAP_READWRITE (0x%08X)\n", V4L2_CAP_READWRITE);
	}
	if (cap.capabilities & V4L2_CAP_ASYNCIO) {
		printf("        V4L2_CAP_ASYNCIO (0x%08X)\n", V4L2_CAP_ASYNCIO);
	}
	if (cap.capabilities & V4L2_CAP_STREAMING) {
		printf("        V4L2_CAP_STREAMING (0x%08X)\n", V4L2_CAP_STREAMING);
	}
	if (cap.capabilities & V4L2_CAP_READWRITE) {
		printf("        V4L2_CAP_READWRITE (0x%08X)\n", V4L2_CAP_READWRITE);
	}
}


void dumpCropCapabilities(struct v4l2_cropcap cropcap) {
	printf("V4L2 device crop capability:\n");
	printf("    type: %d\n", cropcap.type);
	printf("    bounds (l:t:w:h): %d:%d:%d:%d\n", 	cropcap.bounds.left, 
							cropcap.bounds.top, 
							cropcap.bounds.width, 
							cropcap.bounds.height);
	printf("    defrect (l:t:w:h): %d:%d:%d:%d\n",	cropcap.defrect.left,
							cropcap.defrect.top, 
							cropcap.defrect.width, 
							cropcap.defrect.height);
	printf("    pixelaspect(numerator:denominator): %d:%d\n",
							cropcap.pixelaspect.numerator,
							cropcap.pixelaspect.denominator);
}

char* getStringColorSpace(int colorSpace) {
	switch(colorSpace) {
		/* ITU-R 601 -- broadcast NTSC/PAL */
		case 1: return "V4L2_COLORSPACE_SMPTE170M";

		/* 1125-Line (US) HDTV */
		case 2: return "V4L2_COLORSPACE_SMPTE240M";

		/* HD and modern captures. */
		case 3: return "V4L2_COLORSPACE_REC709";

		/* broken BT878 extents (601, luma range 16-253 instead of 16-235) */
		case 4: return "V4L2_COLORSPACE_BT878";

		/* These should be useful.  Assume 601 extents. */
		case 5: return "V4L2_COLORSPACE_470_SYSTEM_M";
		case 6: return "V4L2_COLORSPACE_470_SYSTEM_BG";

		/* I know there will be cameras that send this.  So, this is
		 * unspecified chromaticities and full 0-255 on each of the
		 * Y'CbCr components
		 */
		case 7: return "V4L2_COLORSPACE_JPEG";

		/* For RGB colourspaces, this is probably a good start. */
		case 8: return "V4L2_COLORSPACE_SRGB";
	}
	return "Unknown color space";
}

void dumpFormat(struct v4l2_format format) {
	printf("V4L2 device format:\n");
	printf("    type: %d\n", format.type);
	printf("    fmt.pix.width: %d\n", format.fmt.pix.width);
	printf("    fmt.pix.height: %d\n", format.fmt.pix.height);
	printf("    fmt.pix.pixelformat: %c%c%c%c (0x%X)\n", (char)format.fmt.pix.pixelformat,
					((char)(format.fmt.pix.pixelformat >> 8)),
					(char)(format.fmt.pix.pixelformat >> 16),
					(char)(format.fmt.pix.pixelformat >> 24),
					format.fmt.pix.pixelformat);
	printf("    fmt.pix.field: %d\n", format.fmt.pix.field);
	printf("    fmt.pix.bytesperline: %d\n", format.fmt.pix.bytesperline);
	printf("    fmt.pix.sizeimage: %d\n", format.fmt.pix.sizeimage);
	printf("    fmt.pix.colorspace: %s (%d)\n", getStringColorSpace(format.fmt.pix.colorspace),
					format.fmt.pix.colorspace);
	printf("    fmt.pix.priv: %d\n", format.fmt.pix.priv);
}

//
// Refer to http://apodeline.free.fr/DOC/libjpeg/libjpeg-2.html
// Refer to http://paulbourke.net/dataformats/yuv/
//
void jpegWrite(char *imageBuffer, int imageWidth, int imageHeight, int fourccPixelFormat, int bytesPerLine) {
	const char *fileName = "v4l2_frame.jpg";
	FILE *outfile;
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	JSAMPROW row_pointer[1];
	int i, j;
	char y1, y2, u, v;
	char *rgbLineBuffer;

	if (fourccPixelFormat != V4L2_PIX_FMT_YUYV) {
		printf("Error: jpeg writer support YUYV only but input is %c%c%c%c\n", 
					(char)fourccPixelFormat,
					(char)(fourccPixelFormat >> 8),
					(char)(fourccPixelFormat >> 16),
					(char)(fourccPixelFormat >> 24));
		return;
	}

	if (imageWidth % 2) {
		printf("Error: image width must be even but image width is %d\n", imageWidth);
		return;
	}

	// Allocate RGB buffer, 3 bytes per pixel.
	rgbLineBuffer = malloc(cinfo.image_width * 3);
	if (rgbLineBuffer == NULL) {
		printf("Error: allocate rgb buffer fails\n");
		return;
	}

	outfile = fopen(fileName, "wb");
	if (outfile == NULL) {
		printf("Error: cannot create jpeg file\n");
		return;
	}

	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);
	jpeg_stdio_dest(&cinfo, outfile);

	cinfo.image_width = imageWidth;
	cinfo.image_height = imageHeight;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, 90, TRUE);
	jpeg_start_compress(&cinfo, TRUE);

	// Convert YUYV to RGB and write to JPEG file line by line
	row_pointer[0] = rgbLineBuffer;
	for (i = 0; i < cinfo.image_height; i++) {
		for (j = 0; j < cinfo.image_width; j += 2) {
			// Convert 2 pixels
			y1 = imageBuffer[(i * bytesPerLine) + (j * 2) + 0];
			u  = imageBuffer[(i * bytesPerLine) + (j * 2) + 1];
			y2 = imageBuffer[(i * bytesPerLine) + (j * 2) + 2];
			v  = imageBuffer[(i * bytesPerLine) + (j * 2) + 3];
			u -= 128;
			v -= 128;
			// RGB for 1st pixel
			rgbLineBuffer[j*3]     = y1 + (1.370705f * v);
			rgbLineBuffer[(j*3)+1] = y1 - (0.698001f * v) - (0.337633f * u);
			rgbLineBuffer[(j*3)+2] = y1 + (1.732446 * u);
			// RGB for 2nd pixel
			rgbLineBuffer[(j*3)+3] = y2 + (1.370705f * v);
			rgbLineBuffer[(j*3)+4] = y2 - (0.698001f * v) - (0.337633f * u);
			rgbLineBuffer[(j*3)+5] = y2 + (1.732446 * u);
		}
		jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}
	free(rgbLineBuffer);

	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);
	fclose(outfile);
	return;
}



