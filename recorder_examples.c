/**
 * 会议录音功能集成示例
 *
 * 演示如何在soundbox中使用会议录音功能
 */

#include "application.h"
#include "gmc_recorder.h"
#include "gmc_protocol.h"

// ============================================================================
// 示例1: 应用层API使用
// ============================================================================

void example1_basic_recording()
{
    auto &app = Application::GetInstance();

    // 开始录音
    app.StartRecording("morning_meeting");

    // 模拟会议进行（实际使用中不需要这个延时）
    vTaskDelay(pdMS_TO_TICKS(60000)); // 录制1分钟

    // 停止录音
    app.StopRecording();
}

// ============================================================================
// 示例2: 带状态监控的录音
// ============================================================================

void example2_recording_with_status()
{
    auto &app = Application::GetInstance();

    ESP_LOGI("APP", "Starting meeting recording...");
    app.StartRecording("team_standup");

    // 定期检查录音状态
    int check_count = 0;
    while (app.IsRecording() && check_count < 20)
    {
        vTaskDelay(pdMS_TO_TICKS(5000)); // 每5秒检查一次

        std::string status = app.GetRecordingStatus();
        ESP_LOGI("APP", "Recording status: %s", status.c_str());

        check_count++;
    }

    if (app.IsRecording())
    {
        app.StopRecording();
        ESP_LOGI("APP", "Recording stopped after time limit");
    }
}

// ============================================================================
// 示例3: 语音控制录音（集成到WakeWord回调）
// ============================================================================

void handle_voice_command(const std::string &command)
{
    auto &app = Application::GetInstance();

    if (command == "开始录音" || command == "start recording")
    {
        if (!app.IsRecording())
        {
            app.StartRecording("voice_activated_meeting");
            ESP_LOGI("APP", "Voice: Recording started");
        }
        else
        {
            ESP_LOGW("APP", "Voice: Already recording");
        }
    }
    else if (command == "停止录音" || command == "stop recording")
    {
        if (app.IsRecording())
        {
            app.StopRecording();
            ESP_LOGI("APP", "Voice: Recording stopped");
        }
        else
        {
            ESP_LOGW("APP", "Voice: Not recording");
        }
    }
    else if (command == "录音状态" || command == "recording status")
    {
        std::string status = app.GetRecordingStatus();
        ESP_LOGI("APP", "Voice: %s", status.c_str());
    }
}

// ============================================================================
// 示例4: GMC协议处理（UART/网络控制）
// ============================================================================

void example4_gmc_command_handler(const cJSON *json)
{
    const cJSON *mode_obj = cJSON_GetObjectItem(json, "mode");
    const cJSON *type_obj = cJSON_GetObjectItem(json, "type");
    const cJSON *cmd_obj = cJSON_GetObjectItem(json, "cmd");

    if (!cJSON_IsString(mode_obj) || !cJSON_IsString(type_obj) || !cJSON_IsString(cmd_obj))
    {
        return;
    }

    const char *type = type_obj->valuestring;
    const char *cmd = cmd_obj->valuestring;

    // 只处理recorder类型的命令
    if (strcmp(type, GMC_TYPE_RECORDER) != 0)
    {
        return;
    }

    const cJSON *data = cJSON_GetObjectItem(json, "data");

    if (strcmp(cmd, "startRecording") == 0)
    {
        gmc_recorder_handle_start(data);
    }
    else if (strcmp(cmd, "stopRecording") == 0)
    {
        gmc_recorder_handle_stop();
    }
    else if (strcmp(cmd, "getRecordingStatus") == 0)
    {
        gmc_recorder_handle_get_status();
    }
}

// ============================================================================
// 示例5: 定时录音任务
// ============================================================================

void example5_scheduled_recording()
{
    auto &app = Application::GetInstance();

    // 每天早上9点自动开始录音
    time_t now = time(NULL);
    struct tm *timeinfo = localtime(&now);

    if (timeinfo->tm_hour == 9 && timeinfo->tm_min == 0)
    {
        if (!app.IsRecording())
        {
            char title[64];
            strftime(title, sizeof(title), "daily_meeting_%Y%m%d_%H%M", timeinfo);
            app.StartRecording(title);
            ESP_LOGI("APP", "Scheduled recording started: %s", title);
        }
    }

    // 每天下午6点自动停止录音
    if (timeinfo->tm_hour == 18 && timeinfo->tm_min == 0)
    {
        if (app.IsRecording())
        {
            app.StopRecording();
            ESP_LOGI("APP", "Scheduled recording stopped");
        }
    }
}

// ============================================================================
// 示例6: 按钮控制录音
// ============================================================================

void example6_button_control(gpio_num_t button_pin)
{
    static bool button_pressed = false;
    auto &app = Application::GetInstance();

    int level = gpio_get_level(button_pin);

    // 按钮按下（假设低电平有效）
    if (level == 0 && !button_pressed)
    {
        button_pressed = true;

        // 切换录音状态
        if (app.IsRecording())
        {
            app.StopRecording();
            ESP_LOGI("APP", "Button: Recording stopped");
        }
        else
        {
            app.StartRecording("button_activated");
            ESP_LOGI("APP", "Button: Recording started");
        }
    }
    // 按钮释放
    else if (level == 1 && button_pressed)
    {
        button_pressed = false;
    }
}

// ============================================================================
// 示例7: 完整的会议录音应用
// ============================================================================

typedef enum
{
    MEETING_STATE_IDLE,
    MEETING_STATE_RECORDING,
    MEETING_STATE_PAUSED,
    MEETING_STATE_COMPLETED
} meeting_state_t;

typedef struct
{
    meeting_state_t state;
    char title[64];
    time_t start_time;
    uint32_t total_duration;
} meeting_session_t;

static meeting_session_t current_meeting = {
    .state = MEETING_STATE_IDLE};

void meeting_start(const char *title)
{
    if (current_meeting.state != MEETING_STATE_IDLE)
    {
        ESP_LOGW("MEETING", "Meeting already in progress");
        return;
    }

    auto &app = Application::GetInstance();

    // 设置会议信息
    strncpy(current_meeting.title, title, sizeof(current_meeting.title) - 1);
    current_meeting.start_time = time(NULL);
    current_meeting.total_duration = 0;
    current_meeting.state = MEETING_STATE_RECORDING;

    // 开始录音
    app.StartRecording(title);

    ESP_LOGI("MEETING", "Meeting started: %s", title);
}

void meeting_stop()
{
    if (current_meeting.state != MEETING_STATE_RECORDING)
    {
        ESP_LOGW("MEETING", "No meeting in progress");
        return;
    }

    auto &app = Application::GetInstance();

    // 停止录音
    app.StopRecording();

    // 计算总时长
    time_t now = time(NULL);
    current_meeting.total_duration = (uint32_t)(now - current_meeting.start_time);
    current_meeting.state = MEETING_STATE_COMPLETED;

    ESP_LOGI("MEETING", "Meeting completed: %s, Duration: %lu seconds",
             current_meeting.title, current_meeting.total_duration);

    // 重置状态
    current_meeting.state = MEETING_STATE_IDLE;
}

meeting_state_t meeting_get_state()
{
    return current_meeting.state;
}

const char *meeting_get_info()
{
    static char info[128];

    if (current_meeting.state == MEETING_STATE_IDLE)
    {
        return "No meeting in progress";
    }

    time_t now = time(NULL);
    uint32_t duration = (uint32_t)(now - current_meeting.start_time);

    snprintf(info, sizeof(info),
             "Meeting: %s, Duration: %02lu:%02lu",
             current_meeting.title,
             duration / 60,
             duration % 60);

    return info;
}

// ============================================================================
// 主函数示例：在application中集成
// ============================================================================

void setup_meeting_recorder()
{
    // 1. 初始化GMC录音模块
    gmc_recorder_init();

    // 2. 注册语音命令处理
    // (假设已有voice_command回调机制)
    // register_voice_command_handler(handle_voice_command);

    // 3. 注册GMC协议处理
    // (假设已有GMC消息回调机制)
    // register_gmc_message_handler(example4_gmc_command_handler);

    ESP_LOGI("SETUP", "Meeting recorder ready");
}

// 在main.cc或application.cc的Start()中调用
void Application::Start()
{
    // ... 原有初始化代码 ...

    // 初始化会议录音功能
    setup_meeting_recorder();

    // ... 继续原有流程 ...
}
