#include <obs-module.h>
#include <plugin-support.h>
#include <atomic>
#include <vector>
#include <algorithm>

namespace {

constexpr const char *kFilterId = "test_pitchshift_filter";
constexpr const char *kTextFilterName = "TestPitchShiftFilterName";
constexpr const char *kPropSemitone = "TestPitchShiftSemitone";
constexpr const char *kSettingSemitone = "semitone";

constexpr int kSemitoneMin = -12;
constexpr int kSemitoneMax = 12;

constexpr uint32_t kExpectedSampleRate = 44100;

struct TestPitchShiftFilter final {
	obs_source_t *context = nullptr;
	std::atomic<int> semitone{0};

	std::vector<float> outbuf[2];
	obs_audio_data out_audio = {};

	bool warned_sample_rate = false;
};

static void *test_pitchshift_create(obs_data_t *settings, obs_source_t *source)
{
	auto *f = new TestPitchShiftFilter();
	f->context = source;

	const int s = (int)obs_data_get_int(settings, kSettingSemitone);
	f->semitone.store(s, std::memory_order_relaxed);

	for (int ch = 0; ch < 2; ch++) {
		f->outbuf[ch].reserve(4096);
	}

	return f;
}

static void test_pitchshift_update(void *data, obs_data_t *settings)
{
	auto *f = static_cast<TestPitchShiftFilter *>(data);

	const int s = (int)obs_data_get_int(settings, kSettingSemitone);
	f->semitone.store(s, std::memory_order_relaxed);
}

static void test_pitchshift_destroy(void *data)
{
	delete static_cast<TestPitchShiftFilter *>(data);
}

static const char *test_pitchshift_get_name(void *)
{
	return obs_module_text(kTextFilterName);
}

static void test_pitchshift_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, kSettingSemitone, 0);
}

static obs_properties_t *test_pitchshift_get_properties(void *data)
{
	// data は create が返したフィルタ状態
	(void)data;

	obs_properties_t *props = obs_properties_create();
	obs_properties_add_int_slider(props, kSettingSemitone, obs_module_text(kPropSemitone), kSemitoneMin,
				      kSemitoneMax, 1);

	return props;
}

static obs_audio_data *test_pitchshift_filter_audio(void *data, obs_audio_data *audio)
{
	auto *f = static_cast<TestPitchShiftFilter *>(data);

	const uint32_t sr = audio_output_get_sample_rate(obs_get_audio());
	if (sr != kExpectedSampleRate) {
		if (!f->warned_sample_rate) {
			obs_log(LOG_WARNING,
				"[test_pitchshift_filter] sample rate must be %u Hz, but actual is %u Hz. bypassing.",
				kExpectedSampleRate, sr);
			f->warned_sample_rate = true;
		}
		return audio;
	}

	const size_t channels = audio_output_get_channels(obs_get_audio());
	if (channels != 2) {
		return audio;
	}

	// semitone が 0 のときは従来通りバイパスでも良いが、
	// ここでは検証のため、あえて outbuf にコピーして返しても良い。
	const uint32_t frames = audio->frames;

	for (int ch = 0; ch < 2; ch++) {
		f->outbuf[ch].resize(frames);

		const float *in = reinterpret_cast<const float *>(audio->data[ch]);
		float *out = f->outbuf[ch].data();

		std::copy_n(in, frames, out);

		f->out_audio.data[ch] = reinterpret_cast<uint8_t *>(out);
	}

	for (size_t ch = 2; ch < MAX_AV_PLANES; ch++) {
		f->out_audio.data[ch] = nullptr;
	}

	f->out_audio.frames = frames;
	f->out_audio.timestamp = audio->timestamp;
	return &f->out_audio;
}

} // namespace

void register_test_pitchshift_filter(void)
{
	obs_source_info info = {};
	info.id = kFilterId;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_AUDIO;

	info.get_name = test_pitchshift_get_name;
	info.create = test_pitchshift_create;
	info.update = test_pitchshift_update;
	info.destroy = test_pitchshift_destroy;
	info.get_defaults = test_pitchshift_get_defaults;
	info.get_properties = test_pitchshift_get_properties;
	info.filter_audio = test_pitchshift_filter_audio;

	obs_register_source(&info);
}
