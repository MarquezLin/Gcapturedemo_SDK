#ifndef VIDEO_CARD_H_
#define VIDEO_CARD_H_
#include <memory>
#include <Windows.h>
#include <condition_variable>
#include <functional>
#include "task.h"
#include "common/semaphore.h"

enum class CaptureMode
{
    Single,
    Continuous
};

class VideoCard
{
public:
    VideoCard();
    VideoCard(const VideoCard &card) = delete;
    VideoCard &operator=(const VideoCard &card) = delete;

    virtual ~VideoCard();
    /*
     * @fun：初始化
     * @return 0：成功，非0：失败
     */
    int init(int w, int h);
    /*
     * @fun：反初始化
     */
    void uninit();
    /*
     * @fun：开始采集(1920*1080*3)
     * @param[in] cb：回调函数
     * @return 0：成功，非0：失败
     */
    int startCapture(
        CaptureMode mode,
        const std::function<void(uchar *, int, int)> &cb);

    /*
     * @fun：连续采集模式下，抓取下一帧（阻塞直到event到来）。
     *        该函数不会创建线程，调用者可自行决定是否在上层建立线程循环调用。
     * @return 0：成功，非0：失败
     */
    int captureStep();

    /*
     * @fun：连续采集模式下，抓取下一帧（最多等待timeout_ms毫秒）。
     * @return 0：成功，1：超时(无新帧)，其他：失败
     */
    int captureStepTimeout(int timeout_ms);

    /*
     * @fun：停止采集
     */
    void stopCapture();

    /*
     * @fun：开始HDMI输出
     * @param：当设备准备好时，调用外部回调
     */
    int startOutVideo(const std::function<void()> &ready_cb);

    /*
     * @fun：输出模式下，等待板卡ready事件（阻塞），并触发ready_cb。
     *        该函数不会创建线程，调用者可在上层自行循环pump。
     */
    int outputStep();

    /*
     * @fun：停止输出视频
     */
    void stopOutVideo();
    /*
     * @fun：往板卡写入输出的视频数据（1920*1080*3）
     * @param[in] video_data
     * @param[in]
     */
    int outputVideoData(BYTE *data, std::mutex &mtx);

private:
    unsigned int video_height_;
    unsigned int video_width_;

private:
    int openDevices();
    void closeDevices();
    int getDevices(GUID guid, char *devpath, size_t len_devpath);
    int captureSingleShot();

private:
    /*
     * @fun：打开设备
     */
    HANDLE openDevice(const std::string &device_path);

    HANDLE c2h0_device_;
    HANDLE h2c0_device_;
    HANDLE user_device_;
    HANDLE event0_device_;
    HANDLE event1_device_;

    void stopVDMA();
    void writeUser(long address, unsigned int val);
    int writeDevice(HANDLE device, long address, DWORD size, BYTE *buffer);
    int readDevice(HANDLE device, long address, DWORD size, BYTE *buffer);

    void genFPGAVDMAAddr();
    void vtc_stream_run(int isstream, int isrun);
    void config_mm2s_vdma(int num_frame);
    void config_s2mm_vdma(int num_frame);
    void vtc_config(int target_fps, double pixclk_mhz);
    const static int VIDEO_FRAME_STORE_NUM = 16;

private:
    // 接收数据使用
    size_t c2h_fpga_ddr_addr_index_ = 0;
    unsigned char *c2h_align_mem_ = nullptr;
    std::shared_ptr<Semaphore> c2h_sem_ = nullptr;
    std::shared_ptr<std::thread> c2h_event_thread_ = nullptr;
    std::shared_ptr<std::thread> c2h_data_thread_ = nullptr;
    unsigned int c2h_fpga_ddr_addr_[VIDEO_FRAME_STORE_NUM];
    bool stop_c2h_ = false;
    void c2hEventThread();
    void c2hDataThread();
    std::shared_ptr<std::function<void(uchar *, int, int)>> recv_data_cb_ = nullptr;
    bool capturing_ = false;

private:
    // 发送数据使用
    size_t h2c_fpga_ddr_addr_index_ = 0;
    unsigned char *h2c_align_mem_ = nullptr;
    std::shared_ptr<Semaphore> h2c_sem_ = nullptr;
    std::shared_ptr<std::thread> h2c_event_thread_ = nullptr;
    unsigned int h2c_fpga_ddr_addr_[VIDEO_FRAME_STORE_NUM];
    bool stop_h2c_ = false;
    void h2cEventThread();
    std::shared_ptr<Worker> write_worker_;
    std::shared_ptr<std::function<void()>> output_ready_cb_ = nullptr;
    bool outputing_ = false;

private:
    int32_t image_bytes_count_ = 0;
    bool is_initialized_ = false;
};

#endif // VideoCard_H
