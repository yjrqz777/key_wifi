/***************************************************************************************************
 * Author: yjrqz777 3210551161@qq.com
 * Date: 2025-03-23 22:29:49
 * LastEditTime: 2025-04-06 18:19:11
 * LastEditors: yjrqz777 3210551161@qq.com
 * Description: 
 * FilePath: /key_wifi/components/myusb/virtualfat.c
 * @YJRQZ777
***************************************************************************************************/

#include "myusb.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "usbd_core.h"
#include "usbd_msc.h"


// #include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_system.h"

#include "cJSON.h"

#ifdef CONFIG_CHERRYDAP_USE_MSC




static const char *TAG = "virtual FATFS";

#define SECTOR_SIZE         512     // 每扇区字节数
#define CLUSTER_SIZE        4       // 每簇扇区数（簇大小 = 512 * 4=2048字节）
#define RESERVED_SECTORS    1       // 保留扇区数（引导扇区）
#define FAT_COPIES          1       // FAT表副本数（简化设计）
#define ROOT_ENTRIES        512     // 根目录条目数（FAT16标准）
#define SECTORS_PER_FAT     10     // FAT表占用的扇区数（需计算）

// 总扇区数 = 64 *1024 * 1024 / 512 = 131072
#define TOTAL_SECTORS       (10240)

/*
DATA_START_SECTOR = 
    保留扇区数（RESERVED_SECTORS） + 
    FAT表数量（FAT_COPIES） × 单个FAT表占用的扇区数（SECTORS_PER_FAT） + 
    根目录占用的扇区数

组件	            计算方式	                                            示例值（5MB FAT12）
​保留扇区数	        RESERVED_SECTORS	                                    1（引导扇区）
​FAT表总扇区数	    FAT_COPIES × SECTORS_PER_FAT	                        1 × 10 = 10
​根目录扇区数	    (ROOT_ENTRIES × 32 + SECTOR_SIZE - 1) / SECTOR_SIZE	    (512 × 32 + 512 -1) / 512 = ​32
​数据区起始扇区	    1 + 10 + 32 = 43	                                     ​43

*/
#define DATA_START_SECTOR   (43)


/* 新增预置文本和文件元数据 */
#define FILE_CONTENT      "This is virtual Fat !!! \n\
The config file name is config.json\n\
\n\
{\n\
\"sta_wifi\": [\n\
    {\n\
    \"note\": \"Don't be the same with ap_wifi\",\n\
    \"en\": 1,\n\
    \"ssid\": \"sta_wifi\",\n\
    \"password\": \"12345678\"\n\
    }\n\
],\n\
\"ap_wifi\": [\n\
    {\n\
    \"note\": \"Don't be the same with sta_wifi\",\n\
    \"en\": 1,\n\
    \"ssid\": \"test\",\n\
    \"password\": \"12345678\"\n\
    }\n\
]\n\
}\n\
\r\n"
#define FILE_SIZE        (sizeof(FILE_CONTENT) - 1)  // 15字节

// 定义文件占用的簇号（FAT簇号从2开始）
#define FILE_START_CLUSTER  2



/***************************************************************************************************/
static uint8_t root_directory[ROOT_ENTRIES * 32] = {0};

static uint8_t file_data[CLUSTER_SIZE * SECTOR_SIZE] = {0};

static uint8_t fat_table[SECTORS_PER_FAT * SECTOR_SIZE] = {0};


static const uint8_t fat_boot_sector[SECTOR_SIZE] = {
    // 引导跳转指令（3字节）
    0xEB, 0x3C, 0x90, 
    // OEM名称（8字节）
    'C', 'H', 'E', 'R', 'R', 'Y', ' ', ' ',
    // 每扇区字节数（512）
    [0x0B] = 0x00, 0x02, 
    // 每簇扇区数
    [0x0D] = CLUSTER_SIZE,
    // 保留扇区数
    [0x0E] = RESERVED_SECTORS & 0xFF, (RESERVED_SECTORS >> 8) & 0xFF,
    // FAT表数量
    [0x10] = FAT_COPIES,
    // 根目录条目数
    [0x11] = ROOT_ENTRIES & 0xFF, (ROOT_ENTRIES >> 8) & 0xFF,
    // 总扇区数（16位，若超过则用32位字段）
    [0x13] = (TOTAL_SECTORS > 65535) ? 0 : (TOTAL_SECTORS & 0xFF),
    [0x14] = (TOTAL_SECTORS > 65535) ? 0 : ((TOTAL_SECTORS >> 8) & 0xFF),
    // 介质类型（可移动磁盘）
    [0x15] = 0xF8,
    // 每FAT表扇区数（16位）
    [0x16] = SECTORS_PER_FAT & 0xFF, (SECTORS_PER_FAT >> 8) & 0xFF,
    // 每磁道扇区数（假设值）
    [0x18] = 0x20, 0x00,
    // 磁头数（假设值）
    [0x1A] = 0x40, 0x00,
    // 隐藏扇区数
    [0x1C] = 0x00, 0x00, 0x00, 0x00,
    // 总扇区数（32位）
    [0x20] = TOTAL_SECTORS & 0xFF, (TOTAL_SECTORS >> 8) & 0xFF, 
    (TOTAL_SECTORS >> 16) & 0xFF, (TOTAL_SECTORS >> 24) & 0xFF,
    // 驱动器号（0x80为硬盘）
    [0x24] = 0x80,
    // 扩展引导标记
    [0x26] = 0x29,
    // 卷序列号（随机生成）
    [0x27] = 0x12, 0x34, 0x56, 0x78,
    // 卷标（11字节）
    [0x2B] = 0xCE, 0xD2, 0xB5, 0xC4, 0xC5, 0xCC, 0xC5, 0xCC, 0x20, 0x20, 0x20,
    // 文件系统类型（8字节）
    [0x36] = 'F', 'A', 'T', '1', '2', ' ', ' ', ' ',
    // 引导签名（0xAA55小端）
    [0x1FE] = 0x55, 0xAA
};

/***************************************************************************************************/



/***************************************************************************************************
 * 功能描述: 修改根目录条目
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
***************************************************************************************************/

void create_readme_file_entry(void) 
{
    // 指向根目录第一个条目
    uint8_t *entry = root_directory;
    
    // 文件名（8.3格式）
    memcpy(entry, "README  TXT", 11);  // 注意中间用空格填充
    
    // 文件属性：0x20表示普通文件
    entry[0x0B] = 0x20;               
    
    // 起始簇号（小端）
    entry[0x1A] = FILE_START_CLUSTER & 0xFF;        
    entry[0x1B] = (FILE_START_CLUSTER >> 8) & 0xFF; 
    
    // 文件大小（小端）
    entry[0x1C] = FILE_SIZE & 0xFF;                 
    entry[0x1D] = (FILE_SIZE >> 8) & 0xFF;
    entry[0x1E] = (FILE_SIZE >> 16) & 0xFF;
    entry[0x1F] = (FILE_SIZE >> 24) & 0xFF;
}


/***************************************************************************************************
 * 功能描述: 更新FAT表
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
***************************************************************************************************/
void init_fat_table(void) 
{
    // FAT[0]和FAT[1]保留
    fat_table[0] = 0xF8; 
    fat_table[1] = 0xFF;
    fat_table[2] = 0xFF; // FAT[1] = 0xFFFF
    
    // 文件占用的簇2标记为结束（0xFFF）
    // FAT表项布局：| 字节0 | 字节1 | 字节2 | -> 表项0（低12位）和表项1（高12位）
    // 簇2对应表项起始位置：字节3（簇0-1占3字节）
    fat_table[3] = 0xFF; // 0xFFF的存储方式
    fat_table[4] = 0x0F; 
}

/***************************************************************************************************
 * 功能描述: 数据区内容生成
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
***************************************************************************************************/
void prepare_file_content(void) 
{
    // 将文本内容写入簇起始位置
    memcpy(file_data, FILE_CONTENT, FILE_SIZE);
    
    // 剩余空间填充0（可选）
    memset(file_data + FILE_SIZE, 0, sizeof(file_data) - FILE_SIZE);
}

/***************************************************************************************************
 * 功能描述: FAT表项设置（FAT12格式）
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
 * param {uint16_t} cluster
 * param {uint16_t} value
***************************************************************************************************/
void set_fat_entry(uint16_t cluster, uint16_t value) {
    uint32_t index = cluster * 3 / 2; // FAT12每项占1.5字节
    if (cluster % 2 == 0) {
        fat_table[index] = value & 0xFF;
        fat_table[index + 1] = (fat_table[index + 1] & 0xF0) | ((value >> 8) & 0x0F);
    } else {
        fat_table[index] = (fat_table[index] & 0x0F) | ((value << 4) & 0xF0);
        fat_table[index + 1] = (value >> 4) & 0xFF;
    }
}
/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
 * param {uint16_t} start_cluster
 * param {uint32_t} file_size
***************************************************************************************************/
// void update_fat_table(uint16_t start_cluster, uint32_t file_size) {
//     // 计算需要的簇数
//     uint32_t clusters_needed = (file_size + CLUSTER_SIZE * SECTOR_SIZE - 1) / (CLUSTER_SIZE * SECTOR_SIZE);
    
//     // 遍历簇链并更新FAT
//     for (uint16_t i = 0; i < clusters_needed; i++) {
//         uint16_t current_cluster = start_cluster + i;
//         if (i == clusters_needed - 1) {
//             // 最后一个簇标记为结束
//             set_fat_entry(current_cluster, 0xFFF);
//         } else {
//             // 指向下一个簇
//             set_fat_entry(current_cluster, current_cluster + 1);
//         }
//     }
// }
/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
 * param {uint8_t} *buffer
 * param {uint32_t} length
***************************************************************************************************/
void parse_fat16_directory(uint8_t *buffer, uint32_t length) {
    for (int i = 0; i < length; i += 32) {
        uint8_t *entry = buffer + i;

        if (entry[0] == 0x00 || entry[0] == 0xE5) continue; // 跳过空或已删除的条目

        char filename[13];
        memcpy(filename, entry, 8);          // 主名
        filename[8] = '.';
        memcpy(filename + 9, entry + 8, 3); // 扩展名
        filename[12] = '\0';

        uint8_t attr = entry[11];
        if (!(attr & 0x08) && !(attr & 0x10)) { // 普通文件（非卷标或目录）
            printf("文件名: %s\n", filename);
        }
    }
}

uint8_t u8FindJson(uint8_t *buffer, uint32_t length)
{
    for (int i = 0; i < length; i += 32) {
        uint8_t *entry = buffer + i;
        // ESP_LOGI(TAG, "entry[0]: %02X", entry[0]);
        if (entry[0] == 0x00 || entry[0] == 0xE5) continue; // 跳过空或已删除的条目

        char filename[13];
        char LastName[5];
        memcpy(filename, entry, 8);          // 主名
        filename[8] = '.';
        memcpy(filename + 9, entry + 8, 3); // 扩展名
        memcpy(LastName, entry + 8, 3); // 扩展名
        LastName[3] = '\0';
        filename[12] = '\0';

        if (memcmp(LastName, "JSO", 3) == 0) {
            // 找到文件名为"config.json"
            printf("1文件名: %s\n", filename);
            return 1; // 返回1表示找到
        } 


        uint8_t attr = entry[11];
        if (!(attr & 0x08) && !(attr & 0x10)) { // 普通文件（非卷标或目录）
            printf("2文件名: %s\n", filename);
        }
    }
    return 0; // 返回0表示未找到
}
/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
 * param {uint8_t} *buffer
 * param {uint32_t} length
***************************************************************************************************/
// void parse_fat16_directory(uint8_t *buffer, uint32_t length) {
//     for (int i = 0; i < length; i += 32) {
//         uint8_t *entry = buffer + i;
        
//         // 跳过空项和已删除项（原有逻辑）
//         if (entry[0] == 0x00 || entry[0] == 0xE5) continue;

//         // 文件名解析（原有逻辑）
//         char filename[13];
//         memcpy(filename, entry, 8);
//         filename[8] = '.';
//         memcpy(filename + 9, entry + 8, 3);
//         filename[12] = '\0';

//         // 清理文件名空格（原有逻辑）
//         for (int j = 7; j >= 0; j--) {
//             if (filename[j] != ' ') break;
//             filename[j] = '\0';
//         }
//         if (filename[9] == ' ') {
//             filename[8] = '\0';
//         } else {
//             for (int j = 11; j >= 9; j--) {
//                 if (filename[j] != ' ') break;
//                 filename[j] = '\0';
//             }
//         }

//         // 属性过滤（原有逻辑）
//         uint8_t attr = entry[11];
//         if (!(attr & 0x08) && !(attr & 0x10)) { // 仅处理普通文件
//             printf("文件名: %-12s | 属性: 0x%02X | Entry数据: ", filename, attr);
            
//             // 新增：完整32字节十六进制输出
//             for (int j = 0; j < 32; j++) {
//                 printf("%02X ", entry[j]);
//                 if (j == 7 || j == 15 || j == 23) printf("| "); // 按8字节分段
//             }
//             printf("\n");
//         }
//     }
// }




/***************************************************************************************************
 * 功能描述: 修改容量报告函数
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
 * param {uint8_t} busid
 * param {uint8_t} lun
 * param {uint32_t} *block_num
 * param {uint32_t} *block_size
***************************************************************************************************/
void usbd_msc_get_cap(uint8_t busid, uint8_t lun, uint32_t *block_num, uint32_t *block_size)
{
    *block_size = SECTOR_SIZE;
    *block_num = TOTAL_SECTORS; // 131072 sectors * 512 = 64MB
}

/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
***************************************************************************************************/
int usbd_msc_sector_read(uint8_t busid, uint8_t lun, uint32_t sector, uint8_t *buffer, uint32_t length) 
{
    // USB_LOG_RAW("read: lun=%d, sector=%lu, buffer = %s, length=%lu\r\n", lun, sector, buffer, length);
    // 1. 引导扇区
    if (sector == 0) { 
        memcpy(buffer, fat_boot_sector, SECTOR_SIZE);
        return 0;
    }

    // 2. FAT表区域
    if (sector >= RESERVED_SECTORS && 
        sector < RESERVED_SECTORS + (FAT_COPIES * SECTORS_PER_FAT)) 
    {
        uint32_t offset = (sector - RESERVED_SECTORS) * SECTOR_SIZE;
        memcpy(buffer, fat_table + offset, length);
        return 0;
    }

    // 3. 根目录区
    uint32_t root_start = RESERVED_SECTORS + (FAT_COPIES * SECTORS_PER_FAT);
    if (sector >= root_start && 
        sector < root_start + (ROOT_ENTRIES * 32 / SECTOR_SIZE)) 
    {
        uint32_t offset = (sector - root_start) * SECTOR_SIZE;
        memcpy(buffer, root_directory + offset, length);
        return 0;
    }

    // 4. 数据区处理
    uint32_t data_sector = sector - DATA_START_SECTOR;
    uint32_t cluster_num = data_sector / CLUSTER_SIZE + 2; // 簇号从2开始
    
    // 检查是否在文件簇范围内
    if (cluster_num == FILE_START_CLUSTER) {
        uint32_t cluster_offset = (data_sector % CLUSTER_SIZE) * SECTOR_SIZE;
        memcpy(buffer, file_data + cluster_offset, length);
    } else {
        memset(buffer, 0, length); // 其他区域返回空
    }
    
    return 0;
}
/***************************************************************************************************
 * 功能描述: 
 * 输入参数: 
 * 输出参数: 
 * 返 回 值: 
 * 其它说明: 
 * param {uint8_t} busid
 * param {uint8_t} lun
 * param {uint32_t} sector
 * param {uint8_t} *buffer
 * param {uint32_t} length
***************************************************************************************************/
char content[512] = {0};


int usbd_msc_sector_write(uint8_t busid, uint8_t lun, uint32_t sector, uint8_t *buffer, uint32_t length)
{
    // USB_LOG_RAW("write: lun=%d, sector=%lu, buffer = %s, length=%lu\r\n", lun, sector, buffer, length);
    uint8_t u8IFconfig = 0;
    // 检测根目录区写入
    uint32_t root_start = RESERVED_SECTORS + (FAT_COPIES * SECTORS_PER_FAT);
    uint32_t root_sectors = (ROOT_ENTRIES * 32 + SECTOR_SIZE - 1) / SECTOR_SIZE;

    if (sector >= root_start && sector < root_start + root_sectors) {
        // parse_fat16_directory(buffer, length);
        u8IFconfig = u8FindJson(buffer, length);
    }
    if (sector >= DATA_START_SECTOR && length>0) 
    {
        USB_LOG_RAW("write: lun=%d, sector=%lu, buffer = %s, length=%lu\r\n", lun, sector, buffer, length);

    
    memcpy(content, buffer, 512);
    content[length-1] = '\0';
    
    // 解析JSON
    cJSON *config_json = cJSON_Parse(content);
    
    if (!config_json) {
        // ESP_LOGE(TAG, "JSON解析失败");
        return 0;
    }
    ESP_LOGI(TAG, "原始JSON: %s", cJSON_PrintUnformatted(config_json));
    // ESP_LOGI(TAG, "JSON解析成功");

    // 提取 sta_wifi 的 en 值
    cJSON *sta_wifi = cJSON_GetObjectItem(config_json, "sta_wifi");
    if (sta_wifi && cJSON_IsArray(sta_wifi)) {
        cJSON *sta_item = cJSON_GetArrayItem(sta_wifi, 0);
        if (sta_item) {
            cJSON *en_sta = cJSON_GetObjectItem(sta_item, "en");
            if (en_sta && cJSON_IsNumber(en_sta)) {
                printf("sta_wifi en 值: %d\n", en_sta->valueint);
            }
        }
    }

    } 
    else
    {
        /* code */
    }
    
    return 0;
}

#endif








#ifdef CONFIG_partition_flash
#define PARTITION_LABEL "storage"

// Mount path for the partition
const char *base_path = "/spiflash";

// Handle of the wear levelling library instance
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;
static const esp_partition_t *msc_partition;
void my_fatfs() 
{


    // 1. 查找FAT分区
    msc_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                            ESP_PARTITION_SUBTYPE_DATA_FAT,
                                            PARTITION_LABEL);
    assert(msc_partition != NULL);

    ESP_LOGI(TAG, "Mounting FAT filesystem");
    // To mount device we need name of device partition, define base_path
    // and allow format partition in case if it is new one and was not formatted before
    const esp_vfs_fat_mount_config_t mount_config = {
            .max_files = 4,
            .format_if_mount_failed = false,
            .allocation_unit_size = CONFIG_WL_SECTOR_SIZE
    };
    esp_err_t err;
    err = esp_vfs_fat_spiflash_mount(base_path, PARTITION_LABEL, &mount_config, &s_wl_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FATFS (%s)", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Opening file");
}
#endif


