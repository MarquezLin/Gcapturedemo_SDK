// #include <QDebug>
#include "video_card.h"

#include <SetupAPI.h>
#include <string.h>
#include <Initguid.h>
#include "common/utils.h"

#pragma comment(lib, "SetupAPI.lib")

// 74c7e4a9-6d5d-4a70-bc0d-20691dff9e9d
DEFINE_GUID(GUID_DEVINTERFACE_XDMA,
            0x74c7e4a9, 0x6d5d, 0x4a70, 0xbc, 0x0d, 0x20, 0x69, 0x1d, 0xff, 0x9e, 0x9d);

static const int DEFAULT_VIDEO_WIDTH = 1920;
static const int DEFAULT_VIDEO_HEIGHT = 1080;
#define MAX_PATH 400
#define FPGA_DDR_START_ADDR 0x00000000
#define VDMA_DATA_BYTES 8
/*****************寄存器地址定义****************/
#define S2MM_VDMA_BASE 0x10000
#define MM2S_VDMA_BASE 0x00000

#define VDMA_S2MM_SR 0x34
#define VDMA_S2MM_CR 0x30
#define VDMA_S2MM_START_ADDR 0xAC
#define VDMA_S2MM_HSIZE 0xA8
#define VDMA_S2MM_STRID 0xA4
#define VDMA_S2MM_VSIZE 0xA0

#define VDMA_MM2S_SR 0x04
#define VDMA_MM2S_CR 0x00
#define VDMA_MM2S_START_ADDR 0x5C
#define VDMA_MM2S_HSIZE 0x58
#define VDMA_MM2S_STRID 0x54
#define VDMA_MM2S_VSIZE 0x50
#define VDMA_STATUS 0x20020

#define CMV_CONTROL_REG 0x20000 // pcie axi lite
#define VTC_CTRL_REG 0x00
#define VTC_SET_VALID_REG 0x18
#define VTC_H_V_TOTAL_REG 0x04
#define VTC_H_V_SIZE_REG 0x08
#define VTC_H_V_START_REG 0x0c
#define VTC_V_SYNC_REG 0x10
#define VTC_H_SYNC_REG 0x14
#define VIDEO_SATU_REG 0x20

#define MAX_BYTES_PER_TRANSFER 0x800000

#define C2H0_NAME "c2h_0"
#define H2C0_NAME "h2c_0"
#define USER_NAME "user"
#define EVENT0_NAME "event_0"
#define EVENT1_NAME "event_1"

VideoCard::VideoCard()
{
    video_width_ = DEFAULT_VIDEO_WIDTH;
    video_height_ = DEFAULT_VIDEO_HEIGHT;
    recv_data_cb_ = nullptr;

    is_initialized_ = false;
}

VideoCard::~VideoCard()
{
    uninit();
}

int VideoCard::init(int w, int h)
{
    int code = openDevices();
    if (0 != code)
    {
        return -1;
    }

    video_width_ = w;
    video_height_ = h;
    h2c_align_mem_ = Utils::allocate_buffer(video_height_ * video_width_ * 3, 4096);
    c2h_align_mem_ = Utils::allocate_buffer(video_height_ * video_width_ * 3, 4096);
    image_bytes_count_ = video_width_ * video_height_ * 3;
    stopVDMA();
    Sleep(10);
    writeUser(0x20020, 255); // set saturation
    genFPGAVDMAAddr();
    vtc_stream_run(0, 0);
    vtc_config(30, 74.25);

    config_s2mm_vdma(VIDEO_FRAME_STORE_NUM);
    config_mm2s_vdma(VIDEO_FRAME_STORE_NUM);

    vtc_stream_run(1, 1);
    is_initialized_ = true;
    printf("init\n");
    return 0;
}

void VideoCard::uninit()
{
    if (!is_initialized_)
    {
        return;
    }
    stopCapture();
    stopOutVideo();
    closeDevices();

    is_initialized_ = false;
    //    closeDevices();
}

int VideoCard::startCapture(CaptureMode mode, const std::function<void(uchar *, int, int)> &cb)
{
    printf("test1\n");
    if (!is_initialized_)
        return -1;
    printf("test2\n");

    recv_data_cb_ = std::make_shared<
        std::function<void(uchar *, int, int)>>(cb);
    printf("test3\n");
    if (mode == CaptureMode::Single)
    {
        printf("start read dma (single)\n");
        return captureSingleShot();
    }

    // ==========
    if (capturing_)
        return 0;

    stop_c2h_ = false;
    c2h_fpga_ddr_addr_index_ = 0;

    c2h_sem_ = std::make_shared<Semaphore>();
    c2h_event_thread_ =
        std::make_shared<std::thread>(&VideoCard::c2hEventThread, this);
    c2h_data_thread_ =
        std::make_shared<std::thread>(&VideoCard::c2hDataThread, this);

    return 0;
}

void VideoCard::stopCapture()
{
    stop_c2h_ = true;
    if (c2h_sem_)
    {
        c2h_sem_->signal();
    }

    if (c2h_event_thread_)
    {
        c2h_event_thread_->join();
        c2h_event_thread_.reset();
        c2h_event_thread_ = nullptr;
    }

    if (c2h_data_thread_)
    {
        c2h_data_thread_->join();
        c2h_data_thread_.reset();
        c2h_data_thread_ = nullptr;
    }
}

void VideoCard::c2hEventThread()
{
    while (1)
    {
        if (stop_c2h_)
        {
            break;
        }
        BYTE val;
        if (readDevice(event1_device_, 0, 1, (BYTE *)&val) < 0)
        {
            break;
        }
        //        qDebug() << "get a event";
        writeUser(S2MM_VDMA_BASE + VDMA_S2MM_SR, 0xffffffff);
        if (c2h_sem_)
        {
            c2h_sem_->signal();
        }
    }
}

void VideoCard::c2hDataThread()
{
    while (1)
    {
        c2h_sem_->wait();
        if (stop_c2h_)
        {
            break;
        }
        // 读取视频数据
        //  qDebug() << "start read dma";
        printf("start read dma\n");
        auto ret = readDevice(c2h0_device_, c2h_fpga_ddr_addr_[c2h_fpga_ddr_addr_index_ % VIDEO_FRAME_STORE_NUM], image_bytes_count_, c2h_align_mem_);
        if (ret < 0)
        {
            break;
        }
        //        qDebug() << "read dma done";
        // 调用注册回调
        if (recv_data_cb_)
        {
            (*recv_data_cb_)(c2h_align_mem_, video_width_, video_height_);
        }
        c2h_fpga_ddr_addr_index_++;
    }
}

int VideoCard::startOutVideo(const std::function<void()> &ready_cb)
{
    if (!is_initialized_)
    {
        return -1;
    }

    if (outputing_)
    {
        return 0;
    }

    stop_h2c_ = false;
    output_ready_cb_ = std::make_shared<std::function<void()>>(ready_cb);
    write_worker_ = std::make_shared<Worker>();
    write_worker_->start();

    h2c_event_thread_ = std::make_shared<std::thread>(std::bind(&VideoCard::h2cEventThread, this));
    outputing_ = true;
    return 0;
}

void VideoCard::stopOutVideo()
{
    stop_h2c_ = true;
    if (h2c_event_thread_)
    {
        h2c_event_thread_->join();
        h2c_event_thread_.reset();
        h2c_event_thread_ = nullptr;
    }

    if (write_worker_)
    {
        write_worker_->stop();
        write_worker_.reset();
        write_worker_ = nullptr;
    }
    outputing_ = false;
}

void VideoCard::h2cEventThread()
{
    while (1)
    {
        if (stop_h2c_)
        {
            break;
        }
        BYTE val;
        readDevice(event0_device_, 0, 1, (BYTE *)&val);
        writeUser(MM2S_VDMA_BASE + VDMA_MM2S_SR, 0xffffffff);
        // 这里原来是释放信号量
        if (write_worker_)
        {
            Task task(*output_ready_cb_);
            write_worker_->pushTask(task); // 异步任务，和原来保持一致
        }
    }
}

int VideoCard::outputVideoData(BYTE *data, std::mutex &mtx)
{
    // data加锁
    {
        std::lock_guard<std::mutex> lck(mtx);
        memcpy(h2c_align_mem_, data, image_bytes_count_);
    }

    writeDevice(h2c0_device_,
                h2c_fpga_ddr_addr_[h2c_fpga_ddr_addr_index_ % VIDEO_FRAME_STORE_NUM],
                image_bytes_count_,
                h2c_align_mem_);
    h2c_fpga_ddr_addr_index_++;
    return 0;
}

int VideoCard::readDevice(HANDLE device, long address, DWORD size, BYTE *buffer)
{
    DWORD rd_size = 0;

    unsigned int transfers;
    int i;
    if (INVALID_SET_FILE_POINTER == SetFilePointer(device, address, NULL, FILE_BEGIN))
    {
        fprintf(stderr, "Error setting file pointer, win32 error code: %ld\n", GetLastError());
        return -3;
    }
    transfers = (unsigned int)(size / MAX_BYTES_PER_TRANSFER);
    for (i = 0; i < transfers; i++)
    {
        if (!ReadFile(device, (void *)(buffer + i * MAX_BYTES_PER_TRANSFER), (DWORD)MAX_BYTES_PER_TRANSFER, &rd_size, NULL))
        {
            return -1;
        }
        if (rd_size != MAX_BYTES_PER_TRANSFER)
        {
            return -2;
        }
    }
    if (!ReadFile(device, (void *)(buffer + i * MAX_BYTES_PER_TRANSFER), (DWORD)(size - i * MAX_BYTES_PER_TRANSFER), &rd_size, NULL))
    {
        return -1;
    }
    if (rd_size != (size - i * MAX_BYTES_PER_TRANSFER))
    {
        return -2;
    }

    return size;
}

void VideoCard::vtc_stream_run(int isstream, int isrun)
{
    int val = 0;
    val = (isstream << 1) | isrun;
    writeUser(CMV_CONTROL_REG + VTC_CTRL_REG, val);
    writeUser(CMV_CONTROL_REG + VTC_SET_VALID_REG, 1);
    writeUser(CMV_CONTROL_REG + VTC_SET_VALID_REG, 0);
}

void VideoCard::vtc_config(int target_fps, double pixclk_mhz)
{
    unsigned short v_total;
    unsigned short h_total;
    unsigned short v_size;
    unsigned short h_size;
    unsigned short v_start;
    unsigned short h_start;
    unsigned short v_sync_start;
    unsigned short v_sync_stop;
    unsigned short h_sync_start;
    unsigned short h_sync_stop;

    unsigned int hblank;
    unsigned int v;
    double one_frame_nsec = 1000.0 / target_fps * 1000000;
    double period = 1000.0 / pixclk_mhz;
    double n_clk = one_frame_nsec / period;
    if (video_height_ == 1080 && video_width_ == 1920)
    {
        v_total = 1125;
        h_total = 2200;
        v_size = 1080;
        h_size = 1920;
        v_start = 10;
        h_start = 60;
        v_sync_start = 0;
        v_sync_stop = 4;
        h_sync_start = 0;
        h_sync_stop = 40;
    }
    else
    {
        h_size = video_width_;
        v_size = video_height_;
        hblank = video_width_ / 4;
        v_total = n_clk / (hblank + video_width_);
        h_total = hblank + video_width_;
        h_start = hblank / 2;
        v_start = (v_total - v_size) / 2;
        v_sync_start = 0;
        v_sync_stop = (v_total - v_size) / 4;
        h_sync_start = 0;
        h_sync_stop = hblank / 4;
    }
    writeUser(0x20004, (v_total << 16) | h_total);
    writeUser(0x20008, (v_size << 16) | h_size);
    writeUser(0x2000c, (v_start << 16) | h_start);
    writeUser(0x20010, (v_sync_start << 16) | v_sync_stop);
    writeUser(0x20014, (h_sync_start << 16) | h_sync_stop);
    writeUser(0x20018, 1);
    writeUser(0x20018, 0);
}

void VideoCard::config_mm2s_vdma(int num_frame)
{
    int i;
    writeUser(MM2S_VDMA_BASE + VDMA_MM2S_CR, 0x04);
    Sleep(100);
    writeUser(MM2S_VDMA_BASE + VDMA_MM2S_CR, 0x01011003); // 0x01011003
    writeUser(MM2S_VDMA_BASE + VDMA_MM2S_HSIZE, video_width_ * 3);
    writeUser(MM2S_VDMA_BASE + VDMA_MM2S_STRID, video_width_ * 3);
    for (i = 0; i < num_frame; i++)
        writeUser(MM2S_VDMA_BASE + VDMA_MM2S_START_ADDR + i * 0x4, h2c_fpga_ddr_addr_[i]);
    writeUser(MM2S_VDMA_BASE + VDMA_MM2S_SR, 0xffffffff);
    writeUser(MM2S_VDMA_BASE + VDMA_MM2S_VSIZE, video_height_);
}

void VideoCard::config_s2mm_vdma(int num_frame)
{
    int i;
    writeUser(S2MM_VDMA_BASE + VDMA_S2MM_CR, 0x4);
    Sleep(100);
    writeUser(S2MM_VDMA_BASE + VDMA_S2MM_CR, 0x01011003);
    writeUser(S2MM_VDMA_BASE + VDMA_S2MM_HSIZE, video_width_ * 3);
    writeUser(S2MM_VDMA_BASE + VDMA_S2MM_STRID, video_width_ * 3);
    for (int i = 0; i < num_frame; i++)
        writeUser(S2MM_VDMA_BASE + VDMA_S2MM_START_ADDR + i * 0x4, c2h_fpga_ddr_addr_[i]);
    writeUser(S2MM_VDMA_BASE + VDMA_S2MM_SR, 0xffffffff);
    writeUser(S2MM_VDMA_BASE + VDMA_S2MM_VSIZE, video_height_);
}

void VideoCard::stopVDMA()
{
    writeUser(S2MM_VDMA_BASE + VDMA_S2MM_CR, 0x4);
    writeUser(MM2S_VDMA_BASE + VDMA_MM2S_CR, 0x4);
}

void VideoCard::genFPGAVDMAAddr()
{
    unsigned long size;
    unsigned long size_align;
    size = video_height_ * video_width_ * 3;
    size_align = (size / VDMA_DATA_BYTES + ((size % VDMA_DATA_BYTES) == 0 ? 0 : 1)) * VDMA_DATA_BYTES;
    for (int i = 0; i < VIDEO_FRAME_STORE_NUM; i++)
    {
        h2c_fpga_ddr_addr_[i] = FPGA_DDR_START_ADDR + size_align * i;
        c2h_fpga_ddr_addr_[i] = FPGA_DDR_START_ADDR + size_align * (VIDEO_FRAME_STORE_NUM + i);
    }
}

void VideoCard::writeUser(long address, unsigned int val)
{
    writeDevice(user_device_, address, 4, (BYTE *)&val);
}

int VideoCard::writeDevice(HANDLE device, long address, DWORD size, BYTE *buffer)
{
    DWORD wr_size = 0;
    unsigned int transfers;
    int i;
    transfers = (unsigned int)(size / MAX_BYTES_PER_TRANSFER);
    if (INVALID_SET_FILE_POINTER == SetFilePointer(device, address, NULL, FILE_BEGIN))
    {
        fprintf(stderr, "Error setting file pointer, win32 error code: %ld\n", GetLastError());
        return -3;
    }
    for (i = 0; i < transfers; i++)
    {
        if (!WriteFile(device, (void *)(buffer + i * MAX_BYTES_PER_TRANSFER), MAX_BYTES_PER_TRANSFER, &wr_size, NULL))
        {
            return -1;
        }
        if (wr_size != MAX_BYTES_PER_TRANSFER)
        {
            return -2;
        }
    }
    if (!WriteFile(device, (void *)(buffer + i * MAX_BYTES_PER_TRANSFER), (DWORD)(size - i * MAX_BYTES_PER_TRANSFER), &wr_size, NULL))
    {
        return -1;
    }
    if (wr_size != (size - i * MAX_BYTES_PER_TRANSFER))
    {
        return -2;
    }
    return size;
}

int VideoCard::getDevices(GUID guid, char *devpath, size_t len_devpath)
{
    SP_DEVICE_INTERFACE_DATA device_interface;
    PSP_DEVICE_INTERFACE_DETAIL_DATA dev_detail;
    DWORD index;
    HDEVINFO device_info;
    wchar_t tmp[256];
    device_info = SetupDiGetClassDevs((LPGUID)&guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (device_info == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "GetDevices INVALID_HANDLE_VALUE\n");
        return -1;
    }

    device_interface.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    // enumerate through devices

    for (index = 0; SetupDiEnumDeviceInterfaces(device_info, NULL, &guid, index, &device_interface); ++index)
    {

        // get required buffer size
        ULONG detailLength = 0;
        if (!SetupDiGetDeviceInterfaceDetail(device_info, &device_interface, NULL, 0, &detailLength, NULL) && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        {
            fprintf(stderr, "SetupDiGetDeviceInterfaceDetail - get length failed\n");
            break;
        }

        // allocate space for device interface detail
        dev_detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, detailLength);
        if (!dev_detail)
        {
            fprintf(stderr, "HeapAlloc failed\n");
            break;
        }
        dev_detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        // get device interface detail
        if (!SetupDiGetDeviceInterfaceDetail(device_info, &device_interface, dev_detail, detailLength, NULL, NULL))
        {
            fprintf(stderr, "SetupDiGetDeviceInterfaceDetail - get detail failed\n");
            HeapFree(GetProcessHeap(), 0, dev_detail);
            break;
        }

        wcscpy(tmp, dev_detail->DevicePath);
        wcstombs(devpath, tmp, 256);
        HeapFree(GetProcessHeap(), 0, dev_detail);
    }

    SetupDiDestroyDeviceInfoList(device_info);

    return index;
}

int VideoCard::openDevices()
{
    // get device path from GUID
    //    char device_base_path[MAX_PATH + 1] = {0};
    //    DWORD num_devices = getDevices(GUID_DEVINTERFACE_XDMA, device_base_path, sizeof(device_base_path));
    //    if (num_devices < 1)
    //    {
    //        return -1;
    //    }

    //    c2h0_device_ = openDevice(device_base_path + std::string("\\") + C2H0_NAME);
    //    if (c2h0_device_ == INVALID_HANDLE_VALUE)
    //    {
    //        return -2;
    //    }

    //    h2c0_device_ = openDevice(device_base_path + std::string("\\") + H2C0_NAME);
    //    if (h2c0_device_ == INVALID_HANDLE_VALUE)
    //    {
    //        return -2;
    //    }

    //    user_device_ = openDevice(device_base_path + std::string("\\") + USER_NAME);
    //    if (user_device_ == INVALID_HANDLE_VALUE)
    //    {
    //        return -3;
    //    }

    //    event0_device_ = openDevice(device_base_path + std::string("\\") + EVENT0_NAME);
    //    if (event0_device_ == INVALID_HANDLE_VALUE)
    //    {
    //        return -4;
    //    }

    //    event1_device_ = openDevice(device_base_path + std::string("\\") + EVENT1_NAME);
    //    if (event1_device_ == INVALID_HANDLE_VALUE)
    //    {
    //        return -5;
    //    }
    //    qDebug() << "open deivces succeed " << event1_device_;
    //    return 0;

    // get device path from GUID
    char device_path[MAX_PATH + 1] = "";
    char device_base_path[MAX_PATH + 1] = "";
    wchar_t device_path_w[MAX_PATH + 1];
    DWORD num_devices = getDevices(GUID_DEVINTERFACE_XDMA, device_base_path, sizeof(device_base_path));
    //    //verbose_msg("Devices found: %d\n", num_devices);
    if (num_devices < 1)
    {
        printf("error\n");
    }
    // extend device path to include target device node (xdma_control, xdma_user etc)
    //    //verbose_msg("Device base path: %s\n", device_base_path);
    strcpy_s(device_path, sizeof device_path, device_base_path);
    strcat_s(device_path, sizeof device_path, "\\");
    strcat_s(device_path, sizeof device_path, C2H0_NAME);
    ////verbose_msg("Device node: %s\n", C2H0_NAME);
    // open device file
    mbstowcs(device_path_w, device_path, sizeof(device_path));
    c2h0_device_ = CreateFile(device_path_w, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (c2h0_device_ == INVALID_HANDLE_VALUE)
    {
        // fprintf(stderr, "Error opening device, win32 error code: %ld\n", GetLastError());
        DWORD err = GetLastError();
        printf("Error opening device, win32 error code: %lu\n", err);
        if (err == ERROR_FILE_NOT_FOUND)
        {
            return -2; // device not found
        }
        return -1; // other open error
                   //
    }
    memset(device_path, 0, sizeof(device_path));
    strcpy_s(device_path, sizeof device_path, device_base_path);
    strcat_s(device_path, sizeof device_path, "\\");
    strcat_s(device_path, sizeof device_path, H2C0_NAME);
    // verbose_msg("Device node: %s\n", H2C0_NAME);
    //  open device file
    mbstowcs(device_path_w, device_path, sizeof(device_path));
    h2c0_device_ = CreateFile(device_path_w, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h2c0_device_ == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "Error opening device, win32 error code: %ld\n", GetLastError());
        printf("Invalid handle, skip IO.\n");
        return -1;
        //
    }
    memset(device_path, 0, sizeof(device_path));
    strcpy_s(device_path, sizeof device_path, device_base_path);
    strcat_s(device_path, sizeof device_path, "\\");
    strcat_s(device_path, sizeof device_path, USER_NAME);
    // verbose_msg("Device node: %s\n", USER_NAME);
    //  open device file
    mbstowcs(device_path_w, device_path, sizeof(device_path));
    user_device_ = CreateFile(device_path_w, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (user_device_ == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "Error opening device, win32 error code: %ld\n", GetLastError());
        printf("Invalid handle, skip IO.\n");
        return -1;
        //
    }
    memset(device_path, 0, sizeof(device_path));
    strcpy_s(device_path, sizeof device_path, device_base_path);
    strcat_s(device_path, sizeof device_path, "\\");
    strcat_s(device_path, sizeof device_path, EVENT0_NAME);
    // verbose_msg("Device node: %s\n", EVENT0_NAME);
    //  open device file
    mbstowcs(device_path_w, device_path, sizeof(device_path));
    event0_device_ = CreateFile(device_path_w, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (event0_device_ == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "Error opening device, win32 error code: %ld\n", GetLastError());
        printf("Invalid handle, skip IO.\n");
        return -1;
        //
    }
    memset(device_path, 0, sizeof(device_path));
    strcpy_s(device_path, sizeof device_path, device_base_path);
    strcat_s(device_path, sizeof device_path, "\\");
    strcat_s(device_path, sizeof device_path, EVENT1_NAME);
    // verbose_msg("Device node: %s\n", EVENT1_NAME);
    //  open device file
    mbstowcs(device_path_w, device_path, sizeof(device_path));
    event1_device_ = CreateFile(device_path_w, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (event1_device_ == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "Error opening device, win32 error code: %ld\n", GetLastError());
        printf("Invalid handle, skip IO.\n");
        return -1;
        //
    }
    return 0;
}

HANDLE VideoCard::openDevice(const std::string &device_path)
{
    wchar_t device_path_w[MAX_PATH + 1] = {0};
    mbstowcs(device_path_w, device_path.c_str(), device_path.size());
    HANDLE device = CreateFile(device_path_w, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    return device;
}

void VideoCard::closeDevices()
{
    CloseHandle(event1_device_);
    CloseHandle(event0_device_);
    CloseHandle(user_device_);
    CloseHandle(h2c0_device_);
    CloseHandle(c2h0_device_);
}

int VideoCard::captureSingleShot()
{
    stop_c2h_ = false;
    c2h_fpga_ddr_addr_index_ = 0;

    // 等一次 event（等 interrupt）
    BYTE val;
    if (readDevice(event1_device_, 0, 1, (BYTE *)&val) < 0)
        return -1;

    // clear status
    writeUser(S2MM_VDMA_BASE + VDMA_S2MM_SR, 0xffffffff);

    printf("start read dma (single)\n");

    auto ret = readDevice(
        c2h0_device_,
        c2h_fpga_ddr_addr_[0],
        image_bytes_count_,
        c2h_align_mem_);
    if (ret < 0)
        return -1;

    if (recv_data_cb_)
    {
        (*recv_data_cb_)(c2h_align_mem_, video_width_, video_height_);
    }

    return 0;
}
