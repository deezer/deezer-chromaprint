#ifndef CHROMAPRINT_FPCALC_H_
#define CHROMAPRINT_FPCALC_H_

#ifdef __cplusplus
extern "C" {
#endif

namespace Chromaprint
{

typedef struct FPCalcInput {
	int cb_size;				// initialize to the size of this structure (allows future versioning)
	int algo;					// version of the fingerprint algorithm (default CHROMAPRINT_ALGORITHM_TEST1)
	int max_length;				// length of the audio data used for fingerprint calculation (default 120)
	const char *file_name;		// file name
} FPCalcInput;

typedef struct FPCalcOutput {
	int cb_size;				// initialize to the size of this structure (allows future versioning)
	int duration;				// version of the fingerprint algorithm (default CHROMAPRINT_ALGORITHM_TEST1)
	char *fingerprint;	// fingerprint
} FPCalcOutput;

typedef struct FPCalcOption {
	const char *name;
	int value;
} FPCalcOption;

typedef struct FPCalcInputFormat {
	const char *name;
	const char *long_name;
	const char *extensions;
} FPCalcInputFormat;

CHROMAPRINT_API HRESULT chromaprint_get_file_fingerprint(FPCalcInput *input, int options_count, FPCalcOption *options, FPCalcOutput *output);
CHROMAPRINT_API HRESULT chromaprint_get_input_formats(int *formats_count, FPCalcInputFormat *formats);
HRESULT decode_audio_file(ChromaprintContext *chromaprint_ctx, const char *file_name, int max_length, int *duration);

}

#ifdef __cplusplus
}
#endif

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#endif
