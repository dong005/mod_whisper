#ifndef __MOD_WHISPER_H__
#define __MOD_WHISPER_H__

#include <switch.h>
#include <netinet/tcp.h>
#include <libks/ks.h>
#include <libwebsockets.h>

#define AUDIO_BLOCK_SIZE 3200
#define SPEECH_BUFFER_SIZE 49152
#define SPEECH_BUFFER_SIZE_MAX 4194304

typedef enum {
	ASRFLAG_READY = (1 << 0),
	ASRFLAG_INPUT_TIMERS = (1 << 1),
	ASRFLAG_START_OF_SPEECH = (1 << 2),
	ASRFLAG_RETURNED_START_OF_SPEECH = (1 << 3),
	ASRFLAG_NOINPUT_TIMEOUT = (1 << 4),
	ASRFLAG_RESULT_PENDING = (1 << 5),
	ASRFLAG_RESULT_READY = (1 << 6),
	ASRFLAG_RETURNED_RESULT = (1 << 7),
	ASRFLAG_TIMEOUT = (1 << 8)
} whisper_flag_t;

typedef struct {
	uint32_t flags;
	char *result_text;
	double result_confidence;
	uint32_t thresh;
	uint32_t silence_ms;
	uint32_t voice_ms;
	int no_input_timeout;
	int speech_timeout;
	switch_bool_t start_input_timers;
	switch_time_t no_input_time;
	switch_time_t speech_time;
	char *grammar;
	char *channel_uuid;
	switch_vad_t *vad;
	switch_buffer_t *audio_buffer;
	switch_mutex_t *mutex;
	kws_t *ws;
	int partial;
	switch_memory_pool_t *pool;

	/* thread related members */
	switch_mutex_t *wsi_mutex;
	int started;
	switch_bool_t wc_connected;
	switch_bool_t wc_error;
	struct lws *wsi;
	struct lws_context *lws_context;
	struct lws_context_creation_info lws_info;
	struct lws_client_connect_info lws_ccinfo;
} whisper_t;

typedef struct {
	char *text;
	char *voice;
	char *engine;
	char *tts_server_url;
	int samplerate;
	const char *channel_uuid;
	switch_memory_pool_t *pool;
	switch_buffer_t *audio_buffer;
	kws_t *ws;
	
	/* Audio data */
	void *audio_buffer_raw;  /* Raw audio data buffer */
	size_t audio_len;        /* Length of audio data */
	
	/* thread related members */
	switch_mutex_t *wsi_mutex;
	int started;
	switch_bool_t wc_connected;
	switch_bool_t wc_error;
	struct lws *wsi;
	struct lws_context *lws_context;
	struct lws_context_creation_info lws_info;
	struct lws_client_connect_info lws_ccinfo;
} whisper_tts_t;

#define RX_BUFFER_SIZE 64 * 1024 * 16 /* warning: RX_BUFFER_SIZE is also TX_BUFFER_SIZE ! it has to be big, otherwise -> latency problems on send()*/

struct whisper_globals {
	char *asr_server_url;
	char *tts_server_url;
	int return_json;
	int auto_reload;
	switch_memory_pool_t *pool;
	ks_pool_t *ks_pool;
};

extern struct whisper_globals globals;

#define WS_STATE_STARTED 1
#define WS_STATE_DESTROY 0

#define WS_TIMEOUT_MS 50

#endif
