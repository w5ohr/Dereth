#!/usr/bin/env python3
"""Extract AC ambient / music tracks from acdata/client_portal.dat -> assets/acmusic/

AC has no dedicated "music" dat file type: the 0x31xxxxxx range is Strings (heritage
blurbs), not audio. All audio lives in the 0x0A wave resources
  [id u32][headerSize i32][dataSize i32][WAVEFORMATEX header][pcm/compressed data]
(same layout ac_sound_export uses). 785 of 786 are PCM (fmtTag 1); one is MPEG
layer-3 (fmtTag 0x55). The long-duration PCM waves are the environmental ambience /
music loops (a 53s stereo bed plus several 10-18s beds), as opposed to the short
combat/UI stingers.

We export the longest PCM waves (the ambient/music beds) to standalone WAV, and LIST
any non-PCM (MP3/compressed) track in the manifest with its DID and format tag — those
need external decoding and are not converted here (per task constraints). Total capped
well under 15 MB.

Output: assets/acmusic/<did>.wav + manifest.json
  { tracks:[{did,file,channels,sampleRate,seconds,bytes}],
    needs_conversion:[{did,formatTag,note}] }
"""
import importlib.util, json, os, struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ACD = os.path.join(ROOT, "acdata")
OUT = os.path.join(ROOT, "assets", "acmusic")
MAX_TRACKS = 6
SIZE_BUDGET = 15 * 1024 * 1024

spec = importlib.util.spec_from_file_location("acme", os.path.join(ROOT, "tools", "ac_model_export.py"))
acme = importlib.util.module_from_spec(spec); spec.loader.exec_module(acme)

FMT_MP3 = 0x55

def wave_meta(d):
    """-> (fmtTag, channels, sampleRate, headerBytes, dataBytes) or None if malformed."""
    if len(d) < 26:
        return None
    _id, hsz, dsz = struct.unpack_from("<Iii", d, 0)
    if hsz < 16 or 12 + hsz + dsz > len(d) or dsz <= 0:
        return None
    tag, ch, sr = struct.unpack_from("<HHI", d, 12)
    return tag, ch, sr, hsz, dsz

def to_wav(d, hsz, dsz):
    header = d[12:12 + hsz]
    data = d[12 + hsz:12 + hsz + dsz]
    return (b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE"
            + b"fmt " + struct.pack("<I", 16) + header[:16]
            + b"data" + struct.pack("<I", len(data)) + data)

def main():
    portal = acme.DatReader(os.path.join(ACD, "client_portal.dat"))
    os.makedirs(OUT, exist_ok=True)

    waves = []            # (seconds, did, tag, ch, sr, hsz, dsz)
    non_pcm = []
    for oid in sorted(portal.files):
        if not (0x0A000000 <= oid <= 0x0AFFFFFF):
            continue
        m = wave_meta(portal.read(oid))
        if not m:
            continue
        tag, ch, sr, hsz, dsz = m
        if tag != 1:
            non_pcm.append((oid, tag, dsz))
            continue
        seconds = dsz / (sr * ch * 2) if sr and ch else 0
        waves.append((seconds, oid, tag, ch, sr, hsz, dsz))

    # ambient/music beds = the longest PCM waves
    waves.sort(reverse=True)
    tracks, total = [], 0
    for seconds, oid, tag, ch, sr, hsz, dsz in waves:
        if len(tracks) >= MAX_TRACKS:
            break
        wav = to_wav(portal.read(oid), hsz, dsz)
        if total + len(wav) > SIZE_BUDGET:
            continue
        fn = "%08X.wav" % oid
        with open(os.path.join(OUT, fn), "wb") as f:
            f.write(wav)
        total += len(wav)
        tracks.append({"did": "%08X" % oid, "file": fn, "channels": ch,
                       "sampleRate": sr, "seconds": round(seconds, 1), "bytes": len(wav)})

    # Non-PCM tracks: the 0x55 payload is already MPEG layer-3 (raw MP3 frames after the
    # WAVEFORMATEX header) — browsers decode MP3 natively (WebAudio decodeAudioData + <audio>),
    # so no external tool is needed. Dump the raw frames to <did>.mp3 and add to the manifest.
    # (Previously these were only LISTED as "needs_conversion" and left unextracted.)
    exported_nonpcm, still_needs = [], []
    for oid, tag, dsz in non_pcm:
        d = portal.read(oid)
        m = wave_meta(d)
        if not m:
            continue
        _t, ch, sr, hsz, _dsz = m
        payload = d[12 + hsz:12 + hsz + dsz]
        avg = struct.unpack_from("<I", d, 12 + 8)[0] if hsz >= 12 else 0   # WAVEFORMATEX nAvgBytesPerSec
        seconds = round(dsz / avg, 1) if avg else 0
        # only 0x55 (MPEG-3) is browser-native; anything else we still can't play, so list it
        if tag == FMT_MP3 and len(payload) >= 2 and payload[0] == 0xFF and (payload[1] & 0xE0) == 0xE0:
            if total + len(payload) <= SIZE_BUDGET:
                fn = "%08X.mp3" % oid
                with open(os.path.join(OUT, fn), "wb") as f:
                    f.write(payload)
                total += len(payload)
                exported_nonpcm.append({"did": "%08X" % oid, "file": fn, "channels": ch,
                                        "sampleRate": sr, "seconds": seconds, "bytes": len(payload),
                                        "format": "mp3"})
                continue
        still_needs.append({"did": "%08X" % oid, "formatTag": tag,
                            "note": "non-PCM format 0x%02X — needs external decode" % tag})

    # PCM WAV beds first (longest → the loopable zone music), then the browser-native MP3 tracks.
    manifest = {"tracks": tracks + exported_nonpcm, "needs_conversion": still_needs}
    with open(os.path.join(OUT, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1)

    print(f"PCM waves scanned: {len(waves)}  non-PCM: {len(non_pcm)}")
    print(f"exported {len(tracks)} PCM + {len(exported_nonpcm)} MP3 tracks, {total/2**20:.1f} MB")
    for t in tracks + exported_nonpcm:
        print(f"  {t['did']}  {t.get('format','wav')}  {t['seconds']}s  {t['channels']}ch {t['sampleRate']}Hz  {t['bytes']//1024} KB")
    for n in still_needs:
        print(f"  STILL needs conversion: {n['did']} (fmt 0x{n['formatTag']:02X})")

if __name__ == "__main__":
    main()
