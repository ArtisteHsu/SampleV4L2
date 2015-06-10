# SampleV4L2

Access V4L2 device by following steps:
 1. Open video device /dev/video0
 2. Query capability. Supprot video capture device only
 3. Query crop (resolution) capability of video capture device
 4. Query video format of video capture device
 5. Request video buffer
 6. Query and mmap buffer created by ioctl VIDIOC_REQBUFS
 7. Queue video buffer and get one frame
 8. Turn on video streaming
 9. Dequeue video buffer
 10. Process video buffer
 11. Turn off video streaming
 12. Unmap buffer
 13. Close device
 
(Step 2~4 are V4L2 device capability query)

(Step 5~7 are allocating video buffer)

(Step 9 is where video image come)
