#pragma once
// A small lossless FLAC encoder for sOUNAiRe: fixed-block frames of 4096 samples,
// fixed predictors of order 0 to 4 with partitioned Rice residuals, verbatim when
// that is smaller, and the best of independent / left-side / right-side / mid-side
// for stereo. No LPC, so the files are a little larger than flac -8 would make,
// but they are plain FLAC that any decoder reads, bit for bit.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace sounaire {

struct FlacBits {
	std::vector<uint8_t> bytes;
	uint64_t acc = 0;
	int n = 0;

	void put(uint64_t v, int bits) {
		while (bits > 0) {
			int take = std::min(bits, 32);
			bits -= take;
			uint64_t part = (v >> bits) & ((take == 64) ? ~0ull : ((1ull << take) - 1));
			acc = (acc << take) | part;
			n += take;
			while (n >= 8) {
				n -= 8;
				bytes.push_back((uint8_t)(acc >> n));
			}
			acc &= (n ? ((1ull << n) - 1) : 0);
		}
	}
	void putSigned(int64_t v, int bits) {
		put((uint64_t)v & ((bits == 64) ? ~0ull : ((1ull << bits) - 1)), bits);
	}
	void putUnary(uint32_t q) {
		while (q >= 32) {
			put(0, 32);
			q -= 32;
		}
		put(1, q + 1);
	}
	void alignByte() {
		if (n)
			put(0, 8 - n);
	}
	size_t size() const {
		return bytes.size();
	}
};

inline uint8_t flacCrc8(const uint8_t *d, size_t len) {
	uint8_t crc = 0;
	for (size_t i = 0; i < len; i++) {
		crc ^= d[i];
		for (int b = 0; b < 8; b++)
			crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
	}
	return crc;
}

inline uint16_t flacCrc16(const uint8_t *d, size_t len) {
	uint16_t crc = 0;
	for (size_t i = 0; i < len; i++) {
		crc ^= (uint16_t)d[i] << 8;
		for (int b = 0; b < 8; b++)
			crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x8005) : (uint16_t)(crc << 1);
	}
	return crc;
}

// bits for a block of residuals coded with Rice parameter k
inline uint64_t flacRiceBits(const std::vector<uint32_t> &u, size_t a, size_t b, int k) {
	uint64_t bits = 0;
	for (size_t i = a; i < b; i++)
		bits += (u[i] >> k) + 1 + k;
	return bits;
}

inline int flacBestRice(const std::vector<uint32_t> &u, size_t a, size_t b, uint64_t &bitsOut) {
	uint64_t sum = 0;
	for (size_t i = a; i < b; i++)
		sum += u[i];
	size_t n = std::max<size_t>(1, b - a);
	int guess = 0;
	while (guess < 30 && ((uint64_t)n << (guess + 1)) < sum)
		guess++;
	int best = guess;
	uint64_t bestBits = flacRiceBits(u, a, b, guess);
	for (int k : {guess - 1, guess + 1}) {
		if (k < 0 || k > 30)
			continue;
		uint64_t bits = flacRiceBits(u, a, b, k);
		if (bits < bestBits) {
			bestBits = bits;
			best = k;
		}
	}
	bitsOut = bestBits;
	return best;
}

struct FlacSubframe {
	int order = -1;  // -1: verbatim
	int partOrder = 0;
	std::vector<int> params;
	std::vector<uint32_t> u;  // zig-zag residuals
	uint64_t bits = 0;
};

// best fixed-predictor subframe for one channel of `bps` bits
inline FlacSubframe flacPlan(const int64_t *x, int n, int bps) {
	FlacSubframe best;
	best.bits = 8 + (uint64_t)n * bps;  // verbatim
	std::vector<int64_t> r(n);
	for (int order = 0; order <= 4 && order < n; order++) {
		for (int i = order; i < n; i++) {
			int64_t p = 0;
			switch (order) {
				case 0: p = 0; break;
				case 1: p = x[i - 1]; break;
				case 2: p = 2 * x[i - 1] - x[i - 2]; break;
				case 3: p = 3 * x[i - 1] - 3 * x[i - 2] + x[i - 3]; break;
				case 4: p = 4 * x[i - 1] - 6 * x[i - 2] + 4 * x[i - 3] - x[i - 4]; break;
			}
			r[i] = x[i] - p;
		}
		std::vector<uint32_t> u(n);
		bool ok = true;
		for (int i = order; i < n; i++) {
			int64_t v = r[i];
			if (v > 2147483647ll || v < -2147483647ll) {
				ok = false;
				break;
			}
			u[i] = (uint32_t)((v << 1) ^ (v >> 63));
		}
		if (!ok)
			continue;
		// partition orders whose first partition still holds more than `order` samples
		for (int po = 0; po <= 8; po++) {
			if (n % (1 << po) != 0)
				break;
			int plen = n >> po;
			if (plen <= order)
				break;
			uint64_t bits = 8 + (uint64_t)order * bps + 2 + 4;
			std::vector<int> params;
			bool fits = true;
			for (int p = 0; p < (1 << po); p++) {
				size_t a = (size_t)p * plen + (p == 0 ? order : 0), b = (size_t)(p + 1) * plen;
				uint64_t pb = 0;
				int k = flacBestRice(u, a, b, pb);
				if (k > 14) {
					fits = false;  // needs the 5-bit parameter coding; verbatim covers it
					break;
				}
				params.push_back(k);
				bits += 4 + pb;
			}
			if (fits && bits < best.bits) {
				best.order = order;
				best.partOrder = po;
				best.params = params;
				best.u = u;
				best.bits = bits;
			}
		}
	}
	return best;
}

inline void flacWriteSubframe(FlacBits &bw, const int64_t *x, int n, int bps, const FlacSubframe &s) {
	if (s.order < 0) {
		bw.put(0, 1);
		bw.put(1, 6);  // verbatim
		bw.put(0, 1);
		for (int i = 0; i < n; i++)
			bw.putSigned(x[i], bps);
		return;
	}
	bw.put(0, 1);
	bw.put(8 | s.order, 6);  // fixed, order
	bw.put(0, 1);
	for (int i = 0; i < s.order; i++)
		bw.putSigned(x[i], bps);
	bw.put(0, 2);  // Rice, 4-bit parameters
	bw.put(s.partOrder, 4);
	int plen = n >> s.partOrder;
	for (int p = 0; p < (1 << s.partOrder); p++) {
		int k = s.params[p];
		bw.put(k, 4);
		int a = p * plen + (p == 0 ? s.order : 0), b = (p + 1) * plen;
		for (int i = a; i < b; i++) {
			uint32_t v = s.u[i];
			bw.putUnary(v >> k);
			if (k)
				bw.put(v & ((1u << k) - 1), k);
		}
	}
}

inline void flacPutUtf8(FlacBits &bw, uint64_t v) {
	if (v < 0x80) {
		bw.put(v, 8);
		return;
	}
	int bytes = v < 0x800 ? 2 : v < 0x10000 ? 3 : v < 0x200000 ? 4 : v < 0x4000000 ? 5 : 6;
	int shift = (bytes - 1) * 6;
	uint8_t lead = (uint8_t)(0xFF << (8 - bytes)) | (uint8_t)(v >> shift);
	bw.put(lead, 8);
	for (int i = bytes - 2; i >= 0; i--)
		bw.put(0x80 | ((v >> (i * 6)) & 0x3F), 8);
}

// Encodes interleaved integer samples (`bps` bits, 16 or 24) to FLAC bytes.
inline std::vector<uint8_t> flacEncode(const std::vector<int32_t> &interleaved, int channels, int sampleRate, int bps) {
	const int BLOCK = 4096;
	const int64_t frames = channels ? (int64_t)interleaved.size() / channels : 0;
	FlacBits out;
	out.put('f', 8); out.put('L', 8); out.put('a', 8); out.put('C', 8);
	// STREAMINFO, the only metadata block
	out.put(1, 1);
	out.put(0, 7);
	out.put(34, 24);
	out.put(BLOCK, 16);
	out.put(BLOCK, 16);
	out.put(0, 24);  // min frame size: unknown
	out.put(0, 24);  // max frame size: unknown
	out.put(sampleRate, 20);
	out.put(channels - 1, 3);
	out.put(bps - 1, 5);
	out.put((uint64_t)frames, 36);
	for (int i = 0; i < 16; i++)
		out.put(0, 8);  // MD5 left unset (allowed)

	std::vector<int64_t> ch[2], mid, side;
	for (int64_t f0 = 0, fi = 0; f0 < frames; f0 += BLOCK, fi++) {
		int n = (int)std::min<int64_t>(BLOCK, frames - f0);
		for (int c = 0; c < channels; c++) {
			ch[c].resize(n);
			for (int i = 0; i < n; i++)
				ch[c][i] = interleaved[(size_t)(f0 + i) * channels + c];
		}
		// channel assignment: 0..1 independent, 8 left/side, 9 right/side, 10 mid/side
		int assign = channels - 1;
		int count = channels;
		FlacSubframe plans[2];
		const int64_t *data[2] = {nullptr, nullptr};
		int widths[2] = {bps, bps};
		if (channels == 2) {
			mid.resize(n);
			side.resize(n);
			for (int i = 0; i < n; i++) {
				side[i] = ch[0][i] - ch[1][i];
				mid[i] = (ch[0][i] + ch[1][i]) >> 1;
			}
			FlacSubframe pl = flacPlan(ch[0].data(), n, bps), pr = flacPlan(ch[1].data(), n, bps);
			FlacSubframe ps = flacPlan(side.data(), n, bps + 1), pm = flacPlan(mid.data(), n, bps);
			uint64_t costs[4] = {pl.bits + pr.bits, pl.bits + ps.bits, ps.bits + pr.bits, pm.bits + ps.bits};
			int pick = (int)(std::min_element(costs, costs + 4) - costs);
			if (pick == 0) { assign = 1; plans[0] = pl; plans[1] = pr; data[0] = ch[0].data(); data[1] = ch[1].data(); }
			else if (pick == 1) { assign = 8; plans[0] = pl; plans[1] = ps; data[0] = ch[0].data(); data[1] = side.data(); widths[1] = bps + 1; }
			else if (pick == 2) { assign = 9; plans[0] = ps; plans[1] = pr; data[0] = side.data(); data[1] = ch[1].data(); widths[0] = bps + 1; }
			else { assign = 10; plans[0] = pm; plans[1] = ps; data[0] = mid.data(); data[1] = side.data(); widths[1] = bps + 1; }
		}
		else {
			plans[0] = flacPlan(ch[0].data(), n, bps);
			data[0] = ch[0].data();
		}

		FlacBits fb;
		fb.put(0x3FFE, 14);  // sync
		fb.put(0, 1);
		fb.put(0, 1);        // fixed block size
		fb.put(7, 4);        // block size: 16-bit (n - 1) at the end of the header
		fb.put(0, 4);        // sample rate: from STREAMINFO
		fb.put(assign, 4);
		fb.put(bps == 16 ? 4 : (bps == 24 ? 6 : 0), 3);
		fb.put(0, 1);
		flacPutUtf8(fb, (uint64_t)fi);
		fb.put(n - 1, 16);
		fb.put(flacCrc8(fb.bytes.data(), fb.bytes.size()), 8);
		for (int c = 0; c < count; c++)
			flacWriteSubframe(fb, data[c], n, widths[c], plans[c]);
		fb.alignByte();
		uint16_t crc = flacCrc16(fb.bytes.data(), fb.bytes.size());
		fb.put(crc, 16);
		out.bytes.insert(out.bytes.end(), fb.bytes.begin(), fb.bytes.end());
	}
	return out.bytes;
}

// float samples (one vector per channel) to a FLAC file of `bps` bits
inline bool flacWriteFile(const std::string &path, const std::vector<float> *chans, int channels, int64_t frames, int sampleRate, int bps) {
	bps = bps <= 16 ? 16 : 24;
	const double scale = (double)(1 << (bps - 1));
	std::vector<int32_t> pcm((size_t)frames * channels);
	for (int64_t i = 0; i < frames; i++) {
		for (int c = 0; c < channels; c++) {
			double v = std::round((double)chans[c][i] * scale);
			pcm[(size_t)i * channels + c] = (int32_t)std::max(-scale, std::min(scale - 1, v));
		}
	}
	std::vector<uint8_t> bytes = flacEncode(pcm, channels, sampleRate, bps);
	FILE *f = std::fopen(path.c_str(), "wb");
	if (!f)
		return false;
	bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
	std::fclose(f);
	return ok;
}

}  // namespace sounaire
