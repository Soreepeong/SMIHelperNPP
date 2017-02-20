#pragma once
#include<Windows.h>
#include<vector>
#include <Mmreg.h>
#include <mutex>

class WaveView {
private:
	int mDerivationSize = 7;
	std::vector<float*> mBlocks;
	std::vector<int> mKeyframes;
	WAVEFORMATEX *mFormat;
	BYTE *mBitmap = NULL;
	int mStartSample = 0;
	int mSampleCount;
	double mZoom = 1;
	bool mChanged;
	BITMAPINFO mBitmapInfo = { 0 };

	std::mutex mBlockEditLock;

	int mSelectionLeft;
	int mSelectionRight;

	int GetColor(float strength, int colorset);

public:
	WaveView(WAVEFORMATEX *format, int sampleCount);
	~WaveView();

	int GetBlockSize() const { return 2 << mDerivationSize; }
	void FillBlock(char* decoded);
	void AddKeyframe(double time);

	void Zoom(double d);
	void ZoomTo(double d);
	void Scroll(double d);
	void ScrollTo(int frame);
	int GetScroll() const { return mStartSample; };
	double GetZoom() const { return mZoom; };
	int GetX(int sampleIndex) const;
	int GetSampleIndex(int x) const;
	bool HasChanged() const { return mChanged; };
	bool SetParameters(int width, int height);
	void SetSelection(int left, int right) {
		if (mSelectionLeft == left && mSelectionRight == right)
			return;
		mSelectionLeft = left;
		mSelectionRight= right;
		mChanged = true;
	}
	void DrawBitmap();
	const BYTE* GetBitmap() const { return mBitmap; }
	const BITMAPINFO* GetBitmapInfo() const { return &mBitmapInfo; }
};

