#include "gmc_email.h"

// 邮件处理回调函数
void on_email_received(const email_data_t *email) {
    ESP_LOGI("APP", "Processing email: %s", email->title);
    
    // 1. 语音播报邮件内容
    // tts_speak(email->content);
    
    // 2. 翻译邮件
    // char *translated = translate(email->content);
    
    // 3. 生成回复
    char reply_content[512];
    snprintf(reply_content, sizeof(reply_content), 
             "Re: %s\n\n已收到您的邮件", email->title);
    
    // 4. 发送回复给APP
    gmc_email_send_reply(email->id, "Re: " + email->title, reply_content);
    
    // 5. 释放邮件
    gmc_email_free((email_data_t*)email);
}

// 初始化
gmc_email_init();
gmc_email_register_callback(on_email_received);

// 当GMC协议收到readmail命令时会自动调用回调
#include "gmc_audiocontrol.h"

// 初始化为2.1.0系统
gmc_audio_init("2.1.0");

// 设置音量
gmc_audio_set_volume(AUDIO_CH_MAIN_LEFT, 70);

// 设置静音
gmc_audio_set_mute(AUDIO_CH_ALL, 0); // Mute all

// 处理APP命令
gmc_audio_handle_set_volume(data_json);

// 发送通知
gmc_audio_notify_volume_changed(1, 70);