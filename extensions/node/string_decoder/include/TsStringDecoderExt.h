#ifndef TS_STRING_DECODER_EXT_H
#define TS_STRING_DECODER_EXT_H

#ifdef __cplusplus
extern "C" {
#endif

void* ts_string_decoder_create(void* encoding);
void* ts_string_decoder_write(void* decoder, void* buffer);
void* ts_string_decoder_end(void* decoder, void* buffer);
void* ts_string_decoder_get_encoding(void* decoder);

#ifdef __cplusplus
}
#endif

#endif // TS_STRING_DECODER_EXT_H
