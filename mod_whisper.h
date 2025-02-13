#ifndef MOD_WHISPER_H
#define MOD_WHISPER_H

#include <httpd.h>
#include <http_config.h>
#include <http_protocol.h>
#include <http_log.h>
#include <ap_config.h>

// 模块配置结构
typedef struct {
	char *asr_path;     // ASR路径
	char *tts_path;     // TTS路径
	int asr_port;       // ASR端口
	int tts_port;       // TTS端口
} whisper_config;

// 处理函数声明
static int handle_asr_request(request_rec *r);
static int handle_tts_request(request_rec *r);

// 配置相关函数声明
static void *create_whisper_config(apr_pool_t *pool, char *context);
static const char *set_whisper_path(cmd_parms *cmd, void *cfg, const char *arg);
static const char *set_whisper_port(cmd_parms *cmd, void *cfg, const char *arg);

#endif
