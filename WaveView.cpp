#include "WaveView.h"
#include "fft.h"
#undef min
#undef max
#include <algorithm>
#include <math.h>

/*

Largely taken from aegisub-3.2.2/src/audio_renderer_ 

*/

WaveView::WaveView(WAVEFORMATEX *f, int count)
	: mFormat(f)
	, mSampleCount(count)
	, mStartSample(0)
	, mSelectionLeft(0)
	, mSelectionRight(0){
	mBitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	mBitmapInfo.bmiHeader.biBitCount = 32;
	mBitmapInfo.bmiHeader.biPlanes = 1;
	mBitmapInfo.bmiHeader.biCompression = BI_RGB;
	mBitmapInfo.bmiHeader.biClrUsed = 0;
}


WaveView::~WaveView() {
	for (std::vector<float*>::iterator i = mBlocks.begin(); i != mBlocks.end(); ++i)
		delete *i;
	if (mBitmap != NULL)
		delete mBitmap;
}

void WaveView::FillBlock(char* decoded) {
	const int samples = 2 << mDerivationSize;
	float *buf = new float[samples * 3];
	float* fft_in = buf;
	float* fft_real = buf + samples;
	float* fft_imag = buf + samples * 2;
	float* res = new float[samples];
	for (int i = 0; i < samples; i++) {
		float val = 0;
		for (int j = 0; j < mFormat->nChannels; j++) {
			switch (mFormat->wFormatTag) {
				case WAVE_FORMAT_PCM: {
					switch (mFormat->wBitsPerSample) {
						case 8: val += decoded[i*mFormat->nChannels + j] / 128.f; break;
						case 16: val += ((short*)decoded)[i*mFormat->nChannels + j] / 32768.f; break;
						case 24: val += (decoded[(i*mFormat->nChannels + j) * 3] +
							(decoded[(i*mFormat->nChannels + j) * 3 + 1] << 8) +
							(decoded[(i*mFormat->nChannels + j) * 3 + 2] << 16)) / 16777216.f; break;
						case 32: val += ((int*)decoded)[i*mFormat->nChannels + j] / 4294967296.f; break;
					}
					break;
				}
				case WAVE_FORMAT_IEEE_FLOAT: {
					switch (mFormat->wBitsPerSample) {
						case 32: val += ((float*)decoded)[i*mFormat->nChannels + j]; break;
						case 64: val += ((float*)decoded)[i*mFormat->nChannels + j]; break;
					}
					break;
				}
			}
		}
		fft_in[i] = val / mFormat->nChannels;
	}
	FFT fft;
	fft.Transform(samples, fft_in, fft_real, fft_imag);

	float scale_factor = 9 / sqrt(2 * (float)(samples));
	fft_in = res;
	for (size_t si = 1 << mDerivationSize; si > 0; --si) {
		*fft_in++ = log10(sqrt(*fft_real * *fft_real + *fft_imag * *fft_imag) * scale_factor + 1);
		fft_real++; fft_imag++;
	}

	delete buf;
	{
		std::unique_lock<std::mutex> blockLock(mBlockEditLock);
		mBlocks.push_back(res);
	}
	mChanged = true;
}

// http://stackoverflow.com/questions/3018313/algorithm-to-convert-rgb-to-hsv-and-hsv-to-rgb-in-range-0-255-for-both
void WaveView::hsv2rgb(double hh, double s, double v, int &r, int &g, int &b) {
	double      p, q, t, ff;
	long        i;
	hh /= 255;
	s /= 255;
	v /= 255;

	if (s <= 0.0) {       // < is bogus, just shuts up warnings
		r = g = b = 0;
		return;
	}
	if (hh >= 360.0) hh = 0.0;
	hh /= 60.0;
	i = (long)hh;
	ff = hh - i;
	p = v * (1.0 - s);
	q = v * (1.0 - (s * ff));
	t = v * (1.0 - (s * (1.0 - ff)));

	switch (i) {
		case 0:
			r = (int)(v*255);
			g = (int)(t * 255);
			b = (int)(p * 255);
			break;
		case 1:
			r = (int)(q * 255);
			g = (int)(v * 255);
			b = (int)(p * 255);
			break;
		case 2:
			r = (int)(p * 255);
			g = (int)(v * 255);
			b = (int)(t * 255);
			break;

		case 3:
			r = (int)(p * 255);
			g = (int)(q * 255);
			b = (int)(v * 255);
			break;
		case 4:
			r = (int)(t * 255);
			g = (int)(p * 255);
			b = (int)(v * 255);
			break;
		case 5:
		default:
			r = (int)(v * 255);
			g = (int)(p * 255);
			b = (int)(q * 255);
			break;
	}
}

int WaveView::GetColor(float strength, int colorset) {
	int r, g, b;
	if (strength < 0) strength = 0;
	if (strength > 1) strength = 1;
	switch (colorset) {
		case 0: // normal
			hsv2rgb(191 + -128 * strength,
				127 + 128 * strength,
				0 + 255 * strength, r, g, b); break;
		case 1: // selection
			hsv2rgb(191 + -128 * strength,
				127 + 128 * strength,
				63 + 192 * strength, r, g, b); break;
	}
	return RGB(r, g, b);
}

bool WaveView::SetParameters(int width, int height) {
	if (mBitmapInfo.bmiHeader.biWidth == width &&
		mBitmapInfo.bmiHeader.biHeight == height)
		return false;
	mBitmapInfo.bmiHeader.biSizeImage = (mBitmapInfo.bmiHeader.biWidth = width) * (mBitmapInfo.bmiHeader.biHeight = height) * 4;
	if (mBitmap != NULL)
		delete mBitmap;
	mBitmap = (BYTE*)(new int[width*height]);
	return mChanged = true;
}

void WaveView::Zoom(double d) {
	mZoom = std::min(128., std::max(1., mZoom * std::pow(1.2, d)));
	mChanged = true;
}

void WaveView::ZoomTo(double d) {
	mZoom = std::min(128., std::max(1., d));
	mChanged = true;
}

void WaveView::Scroll(double d) {
	mStartSample = mStartSample + (int)(-d*mSampleCount / mZoom / 8);
	if (mStartSample < 0)
		mStartSample = 0;
	else if (mStartSample + (int)(mSampleCount / mZoom) > mSampleCount)
		mStartSample = mSampleCount - (int)(mSampleCount / mZoom);
	mChanged = true;
}

void WaveView::ScrollTo(int frame) {
	mStartSample = frame;
	if (mStartSample < 0)
		mStartSample = 0;
	else if (mStartSample + (int)(mSampleCount / mZoom) > mSampleCount)
		mStartSample = mSampleCount - (int)(mSampleCount / mZoom);
	mChanged = true;
}

int WaveView::GetX(int sampleIndex) const {
	if (sampleIndex < mStartSample)
		return -1;
	if (sampleIndex > mStartSample + (int)(mSampleCount / mZoom))
		return -2;
	return (int)(mZoom * (sampleIndex - mStartSample) * mBitmapInfo.bmiHeader.biWidth / mSampleCount);
}

int WaveView::GetSampleIndex(int x) const {
	return (int)((double) x * mSampleCount / mBitmapInfo.bmiHeader.biWidth / mZoom) + mStartSample;
}

void WaveView::AddKeyframe(double time) {
	mKeyframes.push_back((int)(time * mFormat->nSamplesPerSec));
}

void WaveView::DrawBitmap() {
	if (mBitmap == NULL)
		return;
	std::unique_lock<std::mutex> blockLock(mBlockEditLock);
	const int from_sample = mStartSample;
	const int to_sample = mStartSample + (int)(mSampleCount / mZoom);
	const int samples_per_block = 2 << mDerivationSize;
	const int width = mBitmapInfo.bmiHeader.biWidth;
	const int height = mBitmapInfo.bmiHeader.biHeight;
	const double pixel_s = (double)(to_sample - from_sample) / mFormat->nSamplesPerSec / width;
	const int minband = 0;
	const int maxband = 1 << mDerivationSize;
	const int amplitude_scale = 2;
	for (int i = 0; i < width; i++) {
		int sample_index = (int) (i * pixel_s * mFormat->nSamplesPerSec + from_sample);
		int colorset = sample_index >= mSelectionLeft && sample_index < mSelectionRight ? 1 : 0;
		unsigned int block_index = (unsigned int)(sample_index / samples_per_block);

		std::vector<int>::iterator i_left = std::lower_bound(mKeyframes.begin(), mKeyframes.end(), sample_index),
			i_right = std::upper_bound(mKeyframes.begin(), mKeyframes.end(), sample_index + std::max(1, (int)(pixel_s * mFormat->nSamplesPerSec)));

		if(i_left < i_right) {
			for (int y = 0; y < height; ++y) {
				((int*)mBitmap)[y*width + i] = RGB(255, 0, 255);
			}
			continue;
		}

		if (mBlocks.size() <= block_index) {
			for (int y = 0; y < height; ++y) {
				((int*)mBitmap)[y*width + i] = GetColor(0, colorset);
			}
			continue;
		}
		float *power = mBlocks[block_index];
		// Scale up or down vertically?
		if (height > 1 << mDerivationSize) {
			// Interpolate
			for (int y = 0; y < height; ++y) {
				float ideal = (float)(y + 1.) / height * (maxband - minband) + minband;
				float sample1 = power[(int)floor(ideal) + minband];
				float sample2 = power[(int)ceil(ideal) + minband];
				float frac = ideal - floor(ideal);
				float val = (1 - frac)*sample1 + frac*sample2;
				val *= amplitude_scale;
				((int*)mBitmap)[y*width + i] = GetColor(val, colorset);
			}
		} else {
			// Pick greatest
			for (int y = 0; y < height; ++y) {
				int sample1 = std::max(0, maxband * y / height + minband);
				int sample2 = std::min((1 << mDerivationSize) - 1, maxband * (y + 1) / height + minband);
				float val = *std::max_element(&power[sample1], &power[sample2 + 1]);
				val *= amplitude_scale;
				((int*)mBitmap)[y*width + i] = GetColor(val, colorset);
			}
		}
	}
	mChanged = false;
}