/*
 * FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 * Copyright (C) 2005-2021, Anthony Minessale II <anthm@freeswitch.org>
 *
 * Version: MPL 1.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 *
 * The Initial Developer of the Original Code is
 * Anthony Minessale II <anthm@freeswitch.org>
 * Portions created by the Initial Developer are Copyright (C)
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 * Chris Rienzo <chris@signalwire.com>
 * Seven Du <dujinfang@gmail.com>
 * Ghulam Mustafa <mustafa.pk@gmail.com>
 *
 *
 * mod_whisper.c -- a general purpose module for trancribing audio using websockets
 *
 */

#include "mod_whisper.h"
#include "websock_glue.h"
#include <httpd.h>
#include <http_config.h>
#include <http_protocol.h>
#include <http_log.h>
#include <ap_config.h>

struct whisper_globals whisper_globals;

switch_mutex_t *MUTEX = NULL;
switch_event_node_t *NODE = NULL;

SWITCH_MODULE_LOAD_FUNCTION(mod_whisper_load);
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_whisper_shutdown);
SWITCH_MODULE_RUNTIME_FUNCTION(mod_whisper_runtime);
SWITCH_MODULE_DEFINITION(mod_whisper, mod_whisper_load, mod_whisper_shutdown, mod_whisper_runtime);

// 模块配置结构
typedef struct {
	char *asr_path;     // ASR路径
	char *tts_path;     // TTS路径
	int asr_port;       // ASR端口
	int tts_port;       // TTS端口
} whisper_config;

// 创建默认配置
static void *create_whisper_config(apr_pool_t *pool, char *context) {
	whisper_config *cfg = apr_pcalloc(pool, sizeof(whisper_config));
	if (cfg) {
		// 设置默认值
		cfg->asr_path = "/asr";
		cfg->tts_path = "/tts";
		cfg->asr_port = 8080;
		cfg->tts_port = 8081;
	}
	return cfg;
}

// 配置指令表
static const command_rec whisper_directives[] = {
	AP_INIT_TAKE1("WhisperASRPath", 
		(const char *(*)())ap_set_string_slot,
		(void *)APR_OFFSETOF(whisper_config, asr_path),
		ACCESS_CONF,
		"ASR服务路径"),
	AP_INIT_TAKE1("WhisperTTSPath",
		(const char *(*)())ap_set_string_slot,
		(void *)APR_OFFSETOF(whisper_config, tts_path),
		ACCESS_CONF,
		"TTS服务路径"),
	AP_INIT_TAKE1("WhisperASRPort",
		(const char *(*)())ap_set_int_slot,
		(void *)APR_OFFSETOF(whisper_config, asr_port),
		ACCESS_CONF,
		"ASR服务端口"),
	AP_INIT_TAKE1("WhisperTTSPort",
		(const char *(*)())ap_set_int_slot,
		(void *)APR_OFFSETOF(whisper_config, tts_port),
		ACCESS_CONF,
		"TTS服务端口"),
	{ NULL }
};

// 处理请求的函数
static int whisper_handler(request_rec *r) {
	whisper_config *cfg = ap_get_module_config(r->per_dir_config, 
											 &whisper_module);
	
	// 检查是否是我们的处理程序
	if (!r->handler || strcmp(r->handler, "whisper-handler"))
		return DECLINED;
	
	// 只处理POST请求
	if (r->method_number != M_POST) {
		return HTTP_METHOD_NOT_ALLOWED;
	}
	
	// 检查URL路径
	if (strcmp(r->uri, cfg->asr_path) == 0 || r->server->port == cfg->asr_port) {
		// 处理ASR请求
		return handle_asr_request(r);
	} else if (strcmp(r->uri, cfg->tts_path) == 0 || r->server->port == cfg->tts_port) {
		// 处理TTS请求
		return handle_tts_request(r);
	}
	
	return HTTP_NOT_FOUND;
}

// 注册模块
module AP_MODULE_DECLARE_DATA whisper_module = {
	STANDARD20_MODULE_STUFF,
	create_whisper_config,    // 创建配置
	NULL,                     // 合并配置
	NULL,                     // 创建服务器配置
	NULL,                     // 合并服务器配置
	whisper_directives,       // 配置指令
	whisper_register_hooks    // 注册钩子
};

/* ASR interface */ 

static void whisper_reset_vad(whisper_t *context)
{
	if (context->vad) {
		switch_vad_reset(context->vad);
	}
	context->flags = 0;
	context->result_text = "";
	context->result_confidence = 87.3;
	switch_set_flag(context, ASRFLAG_READY);
	context->no_input_time = switch_micro_time_now();
	if (context->start_input_timers) {
		switch_set_flag(context, ASRFLAG_INPUT_TIMERS);
	}
}

static switch_status_t whisper_open(switch_asr_handle_t *ah, const char *codec, int rate, const char *dest, switch_asr_flag_t *flags)
{
	whisper_t *context;
	char *asr_server = NULL;
	switch_status_t status = SWITCH_STATUS_SUCCESS;


	if (switch_test_flag(ah, SWITCH_ASR_FLAG_CLOSED)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "asr_open attempt on CLOSED asr handle\n");
		return SWITCH_STATUS_FALSE;
	}

	if (!(context = (whisper_t *) switch_core_alloc(ah->memory_pool, sizeof(*context)))) {
		return SWITCH_STATUS_MEMERR;
	}

	context->pool = ah->memory_pool;

	ah->private_info = context;
	codec = "L16";
	ah->codec = switch_core_strdup(ah->memory_pool, codec);

	asr_server = switch_core_strdup(whisper_globals.pool, whisper_globals.asr_server_url);

	if (rate > 16000) {
		ah->native_rate = 16000;
	}

	switch_mutex_init(&context->mutex, SWITCH_MUTEX_NESTED, whisper_globals.pool);

	if (switch_buffer_create_dynamic(&context->audio_buffer, AUDIO_BLOCK_SIZE, AUDIO_BLOCK_SIZE, 0) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to create the audio buffer\n");
		return SWITCH_STATUS_MEMERR;
	}

	status = ws_asr_setup_connection(asr_server, context, whisper_globals.pool);

	if (status != SWITCH_STATUS_SUCCESS) {
		whisper_fire_event(context, "whisper::asr_connection_error");
		ws_asr_close_connection(context);
		return status;
	}

	context->thresh = 400;
	context->silence_ms = 700;
	context->voice_ms = 60;
	context->start_input_timers = 1;
	context->no_input_timeout = 5000;
	context->speech_timeout = 10000;

	context->vad = switch_vad_init(ah->native_rate, 1);
	switch_vad_set_mode(context->vad, -1);
	switch_vad_set_param(context->vad, "thresh", context->thresh);
	switch_vad_set_param(context->vad, "silence_ms", context->silence_ms);
	switch_vad_set_param(context->vad, "voice_ms", context->voice_ms);
	switch_vad_set_param(context->vad, "debug", 1);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "ASR opened\n");

	whisper_reset_vad(context);

	return status;
}

static switch_status_t whisper_load_grammar(switch_asr_handle_t *ah, const char *grammar, const char *name)
{
	whisper_t *context = (whisper_t *)ah->private_info;
	char req_string[100];

	ks_json_t *req = ks_json_create_object();

	if (switch_test_flag(ah, SWITCH_ASR_FLAG_CLOSED)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "asr_open attempt on CLOSED asr handle\n");
		return SWITCH_STATUS_FALSE;
	}

	switch_log_printf(SWITCH_CHANNEL_UUID_LOG(context->channel_uuid), SWITCH_LOG_DEBUG, "load grammar %s\n", grammar);
	context->grammar = switch_core_strdup(ah->memory_pool, grammar);

	snprintf(req_string, sizeof(req_string), "{\"grammar\": \"%s\"}", grammar);
	
	ks_json_add_string_to_object(req, "grammar", grammar);
	strcpy(req_string, ks_json_print_unformatted(req));


	if (ws_send_text(context->wsi, req_string) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Unable to send grammar to websocket server\n");
	}
	
	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t whisper_unload_grammar(switch_asr_handle_t *ah, const char *name)
{
	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t whisper_close(switch_asr_handle_t *ah, switch_asr_flag_t *flags)
{
	whisper_t *context = (whisper_t *)ah->private_info;
	switch_status_t status = SWITCH_STATUS_SUCCESS;

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "ASR close!\n");
	if (switch_test_flag(ah, SWITCH_ASR_FLAG_CLOSED)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Double ASR close!\n");
		return SWITCH_STATUS_FALSE;
	}

	switch_mutex_lock(context->mutex);
	ws_asr_close_connection(context);

	if (context->vad) {
		switch_vad_destroy(&context->vad);
	}

	
	switch_buffer_destroy(&context->audio_buffer);
	switch_set_flag(ah, SWITCH_ASR_FLAG_CLOSED);
	
	switch_mutex_unlock(context->mutex);
	return status;
}

static switch_status_t whisper_feed(switch_asr_handle_t *ah, void *data, unsigned int len, switch_asr_flag_t *flags)
{
	whisper_t *context = (whisper_t *) ah->private_info;
	switch_vad_state_t vad_state;
	int rlen;

	if (switch_test_flag(ah, SWITCH_ASR_FLAG_CLOSED)) {
		return SWITCH_STATUS_BREAK;
	}

	if (switch_test_flag(context, ASRFLAG_RETURNED_RESULT) && switch_test_flag(ah, SWITCH_ASR_FLAG_AUTO_RESUME)) {
		switch_log_printf(SWITCH_CHANNEL_UUID_LOG(context->channel_uuid), SWITCH_LOG_DEBUG, "Auto Resuming\n");
		whisper_reset_vad(context);
	}

	switch_mutex_lock(context->mutex);
	
	if (switch_test_flag(context, ASRFLAG_READY)) {

		vad_state = switch_vad_process(context->vad, (int16_t *)data, len / sizeof(uint16_t));
		
		if (vad_state == SWITCH_VAD_STATE_TALKING) {

			char buf[AUDIO_BLOCK_SIZE];

			switch_buffer_write(context->audio_buffer, data, len);

			if (switch_buffer_inuse(context->audio_buffer) > AUDIO_BLOCK_SIZE) {
				rlen = switch_buffer_read(context->audio_buffer, buf, AUDIO_BLOCK_SIZE);
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Sending data %d %d\n", rlen, context->started);
	
				if (context->started != WS_STATE_STARTED) {
					whisper_fire_event(context, "whisper::asr_connection_error");
					switch_mutex_unlock(context->mutex);
					return SWITCH_STATUS_BREAK; 
				}

				if (ws_send_binary(context->wsi, buf, rlen) != SWITCH_STATUS_SUCCESS) {
					switch_mutex_unlock(context->mutex);
					return SWITCH_STATUS_BREAK;
				}
			} 

		}

		if (vad_state == SWITCH_VAD_STATE_STOP_TALKING || switch_test_flag(context, ASRFLAG_TIMEOUT)) {
			switch_status_t ws_status;

			whisper_fire_event(context, "whisper::asr_stop_talking");

			ws_status = whisper_get_final_transcription(context);
			
			if (ws_status != SWITCH_STATUS_SUCCESS) {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Sendig data for transcription failed\n");

				switch_mutex_unlock(context->mutex);

				return SWITCH_STATUS_BREAK;
			}
			
			// set vad flags to stop detection
			switch_set_flag(context, ASRFLAG_RESULT_PENDING);
			switch_vad_reset(context->vad);
			switch_clear_flag(context, ASRFLAG_READY);
		} else if (vad_state == SWITCH_VAD_STATE_START_TALKING) {
			
			whisper_fire_event(context, "whisper::asr_start_talking");

			switch_set_flag(context, ASRFLAG_START_OF_SPEECH);
			context->speech_time = switch_micro_time_now();
		}
	}

	switch_mutex_unlock(context->mutex);
	
	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t whisper_pause(switch_asr_handle_t *ah)
{
	whisper_t *context = (whisper_t *) ah->private_info;

	if (switch_test_flag(ah, SWITCH_ASR_FLAG_CLOSED)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "asr_pause attempt on CLOSED asr handle\n");
		return SWITCH_STATUS_FALSE;
	}

	switch_log_printf(SWITCH_CHANNEL_UUID_LOG(context->channel_uuid), SWITCH_LOG_DEBUG, "Pausing\n");

	switch_mutex_lock(context->mutex);
	context->flags = 0;

	switch_mutex_unlock(context->mutex);

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t whisper_resume(switch_asr_handle_t *ah)
{
	whisper_t *context = (whisper_t *) ah->private_info;

	if (switch_test_flag(ah, SWITCH_ASR_FLAG_CLOSED)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "asr_resume attempt on CLOSED asr handle\n");
		return SWITCH_STATUS_FALSE;
	}

	switch_log_printf(SWITCH_CHANNEL_UUID_LOG(context->channel_uuid), SWITCH_LOG_DEBUG, "Resuming\n");
	whisper_reset_vad(context);
	
	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t whisper_check_results(switch_asr_handle_t *ah, switch_asr_flag_t *flags)
{
	whisper_t *context = (whisper_t *) ah->private_info;

	if (switch_test_flag(context, ASRFLAG_RESULT_PENDING)) {
		return SWITCH_STATUS_BREAK;
	}
		

	if (switch_test_flag(context, ASRFLAG_RETURNED_RESULT) || switch_test_flag(ah, SWITCH_ASR_FLAG_CLOSED)) {
		return SWITCH_STATUS_BREAK;
	}

	if (!switch_test_flag(context, ASRFLAG_RETURNED_START_OF_SPEECH) && switch_test_flag(context, ASRFLAG_START_OF_SPEECH)) {
		return SWITCH_STATUS_SUCCESS;
	}

	if ((!switch_test_flag(context, ASRFLAG_RESULT_READY)) && (!switch_test_flag(context, ASRFLAG_NOINPUT_TIMEOUT))) {
		if (switch_test_flag(context, ASRFLAG_INPUT_TIMERS) && !(switch_test_flag(context, ASRFLAG_START_OF_SPEECH)) &&
				context->no_input_timeout >= 0 &&
				(switch_micro_time_now() - context->no_input_time) / 1000 >= context->no_input_timeout) {
			switch_log_printf(SWITCH_CHANNEL_UUID_LOG(context->channel_uuid), SWITCH_LOG_DEBUG, "NO INPUT TIMEOUT %" SWITCH_TIME_T_FMT "ms\n", (switch_micro_time_now() - context->no_input_time) / 1000);
			switch_set_flag(context, ASRFLAG_NOINPUT_TIMEOUT);
		} else if (!switch_test_flag(context, ASRFLAG_TIMEOUT) && switch_test_flag(context, ASRFLAG_START_OF_SPEECH) && context->speech_timeout > 0 && (switch_micro_time_now() - context->speech_time) / 1000 >= context->speech_timeout) {
			switch_log_printf(SWITCH_CHANNEL_UUID_LOG(context->channel_uuid), SWITCH_LOG_DEBUG, "SPEECH TIMEOUT %" SWITCH_TIME_T_FMT "ms\n", (switch_micro_time_now() - context->speech_time) / 1000);
			if (switch_test_flag(context, ASRFLAG_START_OF_SPEECH)) {
				switch_set_flag(context, ASRFLAG_TIMEOUT);
				return SWITCH_STATUS_FALSE;
				//switch_set_flag(context, ASRFLAG_RESULT_READY);
			} else {
				switch_set_flag(context, ASRFLAG_NOINPUT_TIMEOUT);
			}
		}
	}

	return switch_test_flag(context, ASRFLAG_RESULT_READY) || switch_test_flag(context, ASRFLAG_NOINPUT_TIMEOUT) ? SWITCH_STATUS_SUCCESS : SWITCH_STATUS_BREAK;
}

static switch_status_t whisper_get_results(switch_asr_handle_t *ah, char **resultstr, switch_asr_flag_t *flags)
{
	whisper_t *context = (whisper_t *) ah->private_info;
	switch_status_t status = SWITCH_STATUS_SUCCESS;

	if (switch_test_flag(context, ASRFLAG_RETURNED_RESULT) || switch_test_flag(ah, SWITCH_ASR_FLAG_CLOSED)) {
		return SWITCH_STATUS_FALSE;
	}

	if (switch_test_flag(context, ASRFLAG_RESULT_READY)) {
		int is_partial = context->partial-- > 0 ? 1 : 0;

		//*resultstr = switch_mprintf("{\"grammar\": \"%s\", \"text\": \"%s\", \"confidence\": %f}", context->grammar, context->result_text, context->result_confidence);

		*resultstr = context->result_text;

		switch_log_printf(SWITCH_CHANNEL_UUID_LOG(context->channel_uuid), SWITCH_LOG_NOTICE, "%sResult: %s\n", is_partial ? "Partial " : "Final ", *resultstr);

		if (is_partial) {
			status = SWITCH_STATUS_MORE_DATA;
		} else {
			status = SWITCH_STATUS_SUCCESS;
		}
	} else if (switch_test_flag(context, ASRFLAG_NOINPUT_TIMEOUT)) {
		switch_log_printf(SWITCH_CHANNEL_UUID_LOG(context->channel_uuid), SWITCH_LOG_DEBUG, "Result: NO INPUT\n");

		*resultstr = switch_mprintf("{\"grammar\": \"%s\", \"text\": \"\", \"confidence\": 0, \"error\": \"no_input\"}", context->grammar);

		status = SWITCH_STATUS_SUCCESS;
	} else if (!switch_test_flag(context, ASRFLAG_RETURNED_START_OF_SPEECH) && switch_test_flag(context, ASRFLAG_START_OF_SPEECH)) {
		switch_set_flag(context, ASRFLAG_RETURNED_START_OF_SPEECH);
		switch_log_printf(SWITCH_CHANNEL_UUID_LOG(context->channel_uuid), SWITCH_LOG_DEBUG, "Result: START OF SPEECH\n");
		status = SWITCH_STATUS_BREAK;
	} else {
		switch_log_printf(SWITCH_CHANNEL_UUID_LOG(context->channel_uuid), SWITCH_LOG_ERROR, "Unexpected call to asr_get_results - no results to return!\n");
		status = SWITCH_STATUS_FALSE;
	}

	if (status == SWITCH_STATUS_SUCCESS) {
		switch_set_flag(context, ASRFLAG_RETURNED_RESULT);
		switch_clear_flag(context, ASRFLAG_READY);
	}

	return status;
}

static switch_status_t whisper_start_input_timers(switch_asr_handle_t *ah)
{
	whisper_t *context = (whisper_t *) ah->private_info;

	if (switch_test_flag(ah, SWITCH_ASR_FLAG_CLOSED)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "asr_start_input_timers attempt on CLOSED asr handle\n");
		return SWITCH_STATUS_FALSE;
	}

	switch_log_printf(SWITCH_CHANNEL_UUID_LOG(context->channel_uuid), SWITCH_LOG_DEBUG, "start_input_timers\n");

	if (!switch_test_flag(context, ASRFLAG_INPUT_TIMERS)) {
		switch_set_flag(context, ASRFLAG_INPUT_TIMERS);
		context->no_input_time = switch_micro_time_now();
	} else {
		switch_log_printf(SWITCH_CHANNEL_UUID_LOG(context->channel_uuid), SWITCH_LOG_INFO, "Input timers already started\n");
	}

	return SWITCH_STATUS_SUCCESS;
}

static void whisper_text_param(switch_asr_handle_t *ah, char *param, const char *val)
{
	whisper_t *context = (whisper_t *) ah->private_info;

	if (!zstr(param) && !zstr(val)) {
		int nval = atoi(val);
		double fval = atof(val);

		if (!strcasecmp("no-input-timeout", param) && switch_is_number(val)) {
			context->no_input_timeout = nval;
			switch_log_printf(SWITCH_CHANNEL_UUID_LOG(context->channel_uuid), SWITCH_LOG_DEBUG, "no-input-timeout = %d\n", context->no_input_timeout);
		} else if (!strcasecmp("speech-timeout", param) && switch_is_number(val)) {
			context->speech_timeout = nval;
			switch_log_printf(SWITCH_CHANNEL_UUID_LOG(context->channel_uuid), SWITCH_LOG_DEBUG, "speech-timeout = %d\n", context->speech_timeout);
		} else if (!strcasecmp("start-input-timers", param)) {
			context->start_input_timers = switch_true(val);
			if (context->start_input_timers) {
				switch_set_flag(context, ASRFLAG_INPUT_TIMERS);
			} else {
				switch_clear_flag(context, ASRFLAG_INPUT_TIMERS);
			}
			switch_log_printf(SWITCH_CHANNEL_UUID_LOG(context->channel_uuid), SWITCH_LOG_DEBUG, "start-input-timers = %d\n", context->start_input_timers);
		} else if (!strcasecmp("vad-mode", param)) {
			switch_log_printf(SWITCH_CHANNEL_UUID_LOG(context->channel_uuid), SWITCH_LOG_DEBUG, "vad-mode = %s\n", val);
			if (context->vad) switch_vad_set_mode(context->vad, nval);
		} else if (!strcasecmp("vad-voice-ms", param) && nval > 0) {
			context->voice_ms = nval;
			switch_vad_set_param(context->vad, "voice_ms", nval);
		} else if (!strcasecmp("vad-silence-ms", param) && nval > 0) {
			context->silence_ms = nval;
			switch_vad_set_param(context->vad, "silence_ms", nval);
		} else if (!strcasecmp("vad-thresh", param) && nval > 0) {
			context->thresh = nval;
			switch_vad_set_param(context->vad, "thresh", nval);
		} else if (!strcasecmp("channel-uuid", param)) {
			context->channel_uuid = switch_core_strdup(ah->memory_pool, val);
			switch_log_printf(SWITCH_CHANNEL_UUID_LOG(context->channel_uuid), SWITCH_LOG_DEBUG, "channel-uuid = %s\n", val);
		} else if (!strcasecmp("result", param)) {
			context->result_text = switch_core_strdup(ah->memory_pool, val);
			switch_log_printf(SWITCH_CHANNEL_UUID_LOG(context->channel_uuid), SWITCH_LOG_DEBUG, "result = %s\n", val);
		} else if (!strcasecmp("confidence", param) && fval >= 0.0) {
			context->result_confidence = fval;
			switch_log_printf(SWITCH_CHANNEL_UUID_LOG(context->channel_uuid), SWITCH_LOG_DEBUG, "confidence = %f\n", fval);
		} else if (!strcasecmp("partial", param) && switch_true(val)) {
			context->partial = 3;
			switch_log_printf(SWITCH_CHANNEL_UUID_LOG(context->channel_uuid), SWITCH_LOG_DEBUG, "partial = %d\n", context->partial);
		}
	}
}

/* TTS Interface */

static switch_status_t whisper_speech_open(switch_speech_handle_t *sh, const char *voice_name, int rate, int channels, switch_speech_flag_t *flags)
{
	whisper_tts_t *context = switch_core_alloc(sh->memory_pool, sizeof(whisper_tts_t));
	switch_status_t status = SWITCH_STATUS_FALSE;
	switch_event_t *event = NULL;
	char * tts_server = NULL;
	char * session_uuid =  NULL;

	/* check if session is associated w/ this memory pool */
	switch_core_session_t *session = switch_core_memory_pool_get_data(sh->memory_pool, "__session");
	if (session) {
		session_uuid = switch_core_session_get_uuid(session);
	}
	
	switch_log_printf(SWITCH_CHANNEL_UUID_LOG(context->channel_uuid), SWITCH_LOG_DEBUG, "session-uuid = %s\n", session_uuid);
	switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, "whisper::tts_open");
	switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "WHISPER-voice", voice_name);
	switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "WHISPER-uuid", session_uuid);
	switch_event_fire(&event);

	switch_assert(context);

    if ( voice_name ) {
        context->voice = switch_core_strdup(sh->memory_pool, voice_name);
    } else {
        context->voice = "default";
    }

	context->samplerate = sh->samplerate;
	
	context->pool = sh->memory_pool;

	switch_buffer_create_dynamic(&context->audio_buffer, SPEECH_BUFFER_SIZE, SPEECH_BUFFER_SIZE, SPEECH_BUFFER_SIZE_MAX);

	sh->private_info = context;

	tts_server = switch_core_strdup(whisper_globals.pool, whisper_globals.tts_server_url);

	status = ws_tts_setup_connection(tts_server, context, whisper_globals.pool);

	return status;
}

static switch_status_t whisper_speech_close(switch_speech_handle_t *sh, switch_speech_flag_t *flags)
{
	whisper_tts_t *context = (whisper_tts_t *) sh->private_info;

	ws_tts_close_connection(context);

	if ( context->audio_buffer ) {
		switch_buffer_destroy(&context->audio_buffer);
	}

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t whisper_speech_feed_tts(switch_speech_handle_t *sh, char *text, switch_speech_flag_t *flags)
{
	whisper_tts_t *context = (whisper_tts_t *)sh->private_info;

	unsigned char buffer[LWS_SEND_BUFFER_PRE_PADDING + strlen(text) + LWS_SEND_BUFFER_POST_PADDING];
	unsigned char *p = &buffer[LWS_SEND_BUFFER_PRE_PADDING];

	if (switch_true(switch_core_get_variable("mod_whisper_tts_must_have_channel_uuid")) && zstr(context->channel_uuid)) {
		return SWITCH_STATUS_FALSE;
	}

	if (!zstr(text)) {
		context->text = switch_core_strdup(sh->memory_pool, text);
	}

	memcpy(p, context->text, strlen(context->text));

	if (lws_write(context->wsi, p, strlen(context->text), LWS_WRITE_TEXT) < 0) {
		fprintf(stderr, "Error writing to socket\n");
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Unable to write message\n");
		return -1;
	}		

	while ( (!context->audio_buffer || switch_buffer_inuse(context->audio_buffer) == 0) && context->started == WS_STATE_STARTED ) {
		usleep(30000);
	}
	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t whisper_speech_read_tts(switch_speech_handle_t *sh, void *data, switch_size_t *datalen, switch_speech_flag_t *flags)
{
	whisper_tts_t *context = (whisper_tts_t *)sh->private_info;
	size_t bytes_read;
	
	if ( (bytes_read = switch_buffer_read(context->audio_buffer, data, *datalen)) ) {
		*datalen = bytes_read ;
		return SWITCH_STATUS_SUCCESS;
	}
	return SWITCH_STATUS_FALSE;
}

static void whisper_speech_flush_tts(switch_speech_handle_t *sh)
{
	whisper_tts_t *context = (whisper_tts_t *) sh->private_info;

	if ( context->audio_buffer ) {
	    switch_buffer_zero(context->audio_buffer);
	}
}

static void whisper_speech_text_param_tts(switch_speech_handle_t *sh, char *param, const char *val)
{
	whisper_tts_t *context = (whisper_tts_t *)sh->private_info;
	if (!zstr(param) && !zstr(val)) {
		if (!strcasecmp("channel-uuid", param)) {
			context->channel_uuid = switch_core_strdup(sh->memory_pool, val);
			switch_log_printf(SWITCH_CHANNEL_UUID_LOG(context->channel_uuid), SWITCH_LOG_DEBUG, "channel-uuid = %s\n", val);
		}
	}
}

static void whisper_speech_numeric_param_tts(switch_speech_handle_t *sh, char *param, int val)
{
}

static void whisper_speech_float_param_tts(switch_speech_handle_t *sh, char *param, double val)
{
}

static switch_status_t load_config(void)
{
	char *cf = "whisper.conf";
	switch_xml_t cfg, xml = NULL, param, settings;
	switch_status_t status = SWITCH_STATUS_SUCCESS;

	if (!(xml = switch_xml_open_cfg(cf, &cfg, NULL))) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Open of %s failed\n", cf);
		status = SWITCH_STATUS_FALSE;
		goto done;
	}


	if ((settings = switch_xml_child(cfg, "settings"))) {
		for (param = switch_xml_child(settings, "param"); param; param = param->next) {
			char *var = (char *) switch_xml_attr_soft(param, "name");
			char *val = (char *) switch_xml_attr_soft(param, "value");
			if (!strcasecmp(var, "asr-server-url")) {
				whisper_globals.asr_server_url = switch_core_strdup(whisper_globals.pool, val);
			}
			if (!strcasecmp(var, "tts-server-url")) {
				whisper_globals.tts_server_url = switch_core_strdup(whisper_globals.pool, val);
			}
			if (!strcasecmp(var, "return-json")) {
				whisper_globals.return_json = atoi(val);
			}
		}
	}

  done:
	if (!whisper_globals.asr_server_url) {
		whisper_globals.asr_server_url = switch_core_strdup(whisper_globals.pool, "ws://127.0.0.1:2700");
	}
	if (!whisper_globals.tts_server_url) {
		whisper_globals.tts_server_url = switch_core_strdup(whisper_globals.pool, "ws://127.0.0.1:2600");
	}
	if (xml) {
		switch_xml_free(xml);
	}

	return status;
}

static void do_load(void)
{
	switch_mutex_lock(MUTEX);
	load_config();
	switch_mutex_unlock(MUTEX);
}

static void event_handler(switch_event_t *event)
{
	if (whisper_globals.auto_reload) {
		do_load();
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Whisper Reloaded\n");
	}
}

SWITCH_MODULE_LOAD_FUNCTION(mod_whisper_load)
{
	switch_asr_interface_t *asr_interface;
	switch_speech_interface_t *speech_interface;

	switch_mutex_init(&MUTEX, SWITCH_MUTEX_NESTED, pool);

	whisper_globals.pool = pool;

	// ks_init();

	// ks_pool_open(&whisper_globals.ks_pool);

	if ((switch_event_bind_removable(modname, SWITCH_EVENT_RELOADXML, NULL, event_handler, NULL, &NODE) != SWITCH_STATUS_SUCCESS)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't bind!\n");
	}

	do_load();

	*module_interface = switch_loadable_module_create_module_interface(pool, modname);

	asr_interface = switch_loadable_module_create_interface(*module_interface, SWITCH_ASR_INTERFACE);
	asr_interface->interface_name = "whisper";
	asr_interface->asr_open = whisper_open;
	asr_interface->asr_load_grammar = whisper_load_grammar;
	asr_interface->asr_unload_grammar = whisper_unload_grammar;
	asr_interface->asr_close = whisper_close;
	asr_interface->asr_feed = whisper_feed;
	asr_interface->asr_resume = whisper_resume;
	asr_interface->asr_pause = whisper_pause;
	asr_interface->asr_check_results = whisper_check_results;
	asr_interface->asr_get_results = whisper_get_results;
	asr_interface->asr_start_input_timers = whisper_start_input_timers;
	asr_interface->asr_text_param = whisper_text_param;


	speech_interface = switch_loadable_module_create_interface(*module_interface, SWITCH_SPEECH_INTERFACE);
	speech_interface->interface_name = "whisper";
	speech_interface->speech_open = whisper_speech_open;
	speech_interface->speech_close = whisper_speech_close;
	speech_interface->speech_feed_tts = whisper_speech_feed_tts;
	speech_interface->speech_read_tts = whisper_speech_read_tts;
	speech_interface->speech_flush_tts = whisper_speech_flush_tts;
	speech_interface->speech_text_param_tts = whisper_speech_text_param_tts;
	speech_interface->speech_numeric_param_tts = whisper_speech_numeric_param_tts;
	speech_interface->speech_float_param_tts = whisper_speech_float_param_tts;


	return SWITCH_STATUS_SUCCESS;
}

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_whisper_shutdown)
{
	// ks_pool_close(&whisper_globals.ks_pool);
	// ks_shutdown();

	switch_event_unbind(&NODE);
	return SWITCH_STATUS_SUCCESS;
}

SWITCH_MODULE_RUNTIME_FUNCTION(mod_whisper_runtime)
{
	return SWITCH_STATUS_TERM;
}

/* For Emacs:
 * Local Variables:
 * mode:c
 * indent-tabs-mode:t
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4 noet:
 */
