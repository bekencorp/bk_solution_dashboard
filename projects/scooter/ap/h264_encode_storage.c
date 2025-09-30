#include <os/os.h>
#include <stdio.h>
#include <os/mem.h>
#include <os/str.h>
#include <components/log.h>
#include <driver/psram.h>
#include <driver/flash.h>
#include <driver/flash_partition.h>
#include "bk_posix.h"

#include "frame_buffer_que.h"

#include <components/avdk_utils/avdk_check.h>
#include <driver/h264_types.h>
#include <driver/timer.h>

#define TAG "h264e_storage"

#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

#define DBG_RAW_E(...) BK_RAW_LOGE(TAG, ##__VA_ARGS__)

#define MAX_NAME_LENGTH 31

typedef struct
{
    uint8_t enable; // 存储task是否启动
    uint8_t first_iframe; // 每次存储图像流，都需要保证首帧为I帧
    uint16_t file_index; // 每次存timer_interval时长的图像流，file_index加1，文件名相应修改
    beken_thread_t thread;
    beken_semaphore_t sem;
    char name[MAX_NAME_LENGTH];
    beken_timer_t timer;
    uint32_t timer_interval; // 每次存储图像流的时间间隔
} storage_info_t;

static storage_info_t *s_storage_info = NULL;

static avdk_err_t bk_vfs_mount_sd0_fatfs(void) {
    int ret = AVDK_ERR_INVAL;
    static bool is_mounted = false;

    if(!is_mounted) {
        struct bk_fatfs_partition partition;
        char *fs_name = NULL;
        fs_name = "fatfs";
        partition.part_type = FATFS_DEVICE;
        partition.part_dev.device_name = FATFS_DEV_SDCARD;
        partition.mount_path = VFS_SD_0_PATITION_0;
        ret = mount("SOURCE_NONE", partition.mount_path, fs_name, 0, &partition);
        is_mounted = true;
    }
    else
    {
        ret = AVDK_ERR_OK;
    }
    return ret;
}

static avdk_err_t bk_mem_save_to_sdcard(char *filename, uint8_t *paddr, uint32_t total_len)
{
    int fd = 0;
    int bytes_write = 0;
    char cFileName[VFS_FILE_MAX_LEN] = {0};

    avdk_err_t ret = bk_vfs_mount_sd0_fatfs();
    if(ret != AVDK_ERR_OK) {
        LOGE("mount sd0 fatfs failed\n");
        return ret;
    }

    sprintf(cFileName, "%s/%s", VFS_SD_0_PATITION_0, filename);

    fd = open(cFileName, O_RDWR | O_CREAT | O_TRUNC);
    if (fd < 0) {
        LOGE("can't open %s\n", cFileName);
        return AVDK_ERR_GENERIC;
    }

    bytes_write = write(fd, (char *)paddr, total_len);
    LOGV("write to %s, bytes_write=%d\n", cFileName, bytes_write);
    close(fd);

    if(bytes_write == total_len) {
        ret = AVDK_ERR_OK;
    }
    else {
        ret = AVDK_ERR_GENERIC;
    }

    return ret;
}

static avdk_err_t bk_mem_append_save_to_sdcard(char *filename, uint8_t *paddr, uint32_t total_len)
{
    int fd = 0;
    int bytes_write = 0;
    char cFileName[VFS_FILE_MAX_LEN] = {0};

    avdk_err_t ret = bk_vfs_mount_sd0_fatfs();
    if(ret != AVDK_ERR_OK) {
        LOGE("mount sd0 fatfs failed\n");
        return ret;
    }

    sprintf(cFileName, "%s/%s", VFS_SD_0_PATITION_0, filename);
    fd = open(cFileName, O_RDWR | O_CREAT | O_APPEND);
    if (fd < 0) {
        LOGE("can't open %s\n", cFileName);
        return AVDK_ERR_GENERIC;
    }

    bytes_write = write(fd, (char *)paddr, total_len);
    LOGV("write to %s, bytes_write=%d\n", cFileName, bytes_write);
    close(fd);

    if(bytes_write == total_len) {
        ret = AVDK_ERR_OK;
    }
    else {
        ret = AVDK_ERR_GENERIC;
    }

    return ret;
}

static void h264e_storage_thread(void *arg)
{
    storage_info_t *info = (storage_info_t *)arg;
    rtos_set_semaphore(&info->sem);
    info->enable = true;
    frame_buffer_t *frame = NULL;
    char filename[41] = {0};
    rtos_start_timer(&info->timer);
    uint16_t file_index = 0xFFFF;
    image_format_t img_format = IMAGE_H264;

    // 先挂载sd卡
    bk_vfs_mount_sd0_fatfs();

    // 先将以前存下的h264帧读取出，并free
    while (1) {
        frame = frame_queue_get_frame(img_format, 0);
        if (frame == NULL) {
            break;
        }
        frame_queue_free(img_format, frame);
    }

    while (info->enable)
    {
        // step 1:确定当前文件中是否有同名文件，如果有则删除
        if (file_index != info->file_index) {
            file_index = info->file_index;
            char old_name[31] = {0};
            info->first_iframe = false; // 重新开始寻找第一个I帧
            sprintf(old_name, "%s/%d_%s", VFS_SD_0_PATITION_0, file_index, info->name);
            sprintf(filename, "%d_%s", file_index, info->name);
            LOGD("%s, file_index=%d, filename=%s\n", __func__, file_index, old_name);
            if (bk_vfs_unlink(old_name) != AVDK_ERR_OK) {
                LOGE("%s, unlink %s failed\n", __func__, old_name);
            }
        }

        // step 2:从队列中读取一帧h264数据
        frame = frame_queue_get_frame(img_format, 50);
        if (frame == NULL) {
            continue;
        }

        // step 3:判断当前读取到的帧是否为I帧
        if (frame->h264_type & (1 << H264_NAL_I_FRAME)) {
            info->first_iframe = true;
        }

        LOGV("%s, %d, first_iframe:%d\n", __func__, frame->sequence, info->first_iframe);
        // step 4:如果读取到了I帧才继续存储下来
        if (info->first_iframe) {
            LOGV("%s, %d, length:%d, fmt:%x\n", __func__, frame->sequence, frame->length, frame->h264_type);
            bk_mem_append_save_to_sdcard(filename, frame->frame, frame->length);
        }

        // step 5:将当前读取到的帧free掉
        frame_queue_free(img_format, frame);
    }

    info->enable = false;
    info->thread = NULL;
    rtos_stop_timer(&info->timer);
    rtos_set_semaphore(&info->sem);
    rtos_delete_thread(NULL);
}

static void h264e_storage_timer_cb(void *arg)
{
    storage_info_t *info = (storage_info_t *)arg;
    info->file_index++;
    LOGD("%s, file_index=%d\n", __func__, info->file_index);
}

avdk_err_t h264e_storage_open(const char *name, uint32_t timer_interval)
{
    avdk_err_t ret = AVDK_ERR_OK;

    if (s_storage_info != NULL) {
        LOGW("%s, h264e storage is busy\n", __func__);
        ret = AVDK_ERR_BUSY;
        return ret;
    }

    if (name == NULL || os_strlen(name) >= 30) {
        LOGW("%s, name is NULL or name length is too long\n", __func__);
        ret = AVDK_ERR_INVAL;
        return ret;
    }

    s_storage_info = (storage_info_t *)os_malloc(sizeof(storage_info_t));
    if (s_storage_info == NULL) {
        ret = AVDK_ERR_NOMEM;
        goto exit;
    }

    memset(s_storage_info, 0, sizeof(storage_info_t));

    if (timer_interval <= 60000) {
        timer_interval = 1000 * 60;// 1 min
    }

    s_storage_info->timer_interval = timer_interval;
    os_memcpy(s_storage_info->name, name, os_strlen(name));

    // create timer
    ret = rtos_init_timer(&s_storage_info->timer, timer_interval, h264e_storage_timer_cb, s_storage_info);
    if (ret != AVDK_ERR_OK) {
        LOGW("%s, create timer failed\n", __func__);
        goto exit;
    }

    // create semaphore
    ret = rtos_init_semaphore(&s_storage_info->sem, 1);
    if (ret != AVDK_ERR_OK) {
        goto exit;
    }

    // create thread
    ret = rtos_create_thread(&s_storage_info->thread,
                            BEKEN_APPLICATION_PRIORITY,
                            "h264e_storage",
                            (beken_thread_function_t)h264e_storage_thread,
                            1024 * 3,
                            (beken_thread_arg_t)s_storage_info);

    if (ret != AVDK_ERR_OK) {
        LOGE("%s, create thread failed\n", __func__);
        goto exit;
    }

    rtos_get_semaphore(&s_storage_info->sem, BEKEN_NEVER_TIMEOUT);

    return ret;
exit:

    if (s_storage_info) {

        if (s_storage_info->sem) {
            rtos_deinit_semaphore(&s_storage_info->sem);
        }

        if (s_storage_info->timer.handle) {
            rtos_deinit_timer(&s_storage_info->timer);
        }

        os_free(s_storage_info);
        s_storage_info = NULL;
    }

    return ret;
}

avdk_err_t h264e_storage_close(void)
{
    avdk_err_t ret = AVDK_ERR_OK;

    if (s_storage_info == NULL) {
        LOGW("%s, h264e storage is not open\n", __func__);
        ret = AVDK_ERR_INVAL;
        goto exit;
    }

    s_storage_info->enable = false;
    rtos_get_semaphore(&s_storage_info->sem, BEKEN_NEVER_TIMEOUT);
    rtos_deinit_semaphore(&s_storage_info->sem);
    rtos_deinit_timer(&s_storage_info->timer);

    os_free(s_storage_info);
    s_storage_info = NULL;

exit:
    return ret;
}