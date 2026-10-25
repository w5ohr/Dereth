// transcript::decodeAudio with AVFoundation: any readable audio file to mono
// float PCM at the requested rate. No microphone, no permission prompt.
#import <AVFoundation/AVFoundation.h>

#include "core/transcript.h"

namespace transcript {

Pcm decodeAudio(const std::string& path, int rate, std::string* error) {
  Pcm out;
  out.rate = rate;
  @autoreleasepool {
    NSError* err = nil;
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    AVAudioFile* file = [[AVAudioFile alloc] initForReading:url error:&err];
    if (!file) {
      if (error) *error = err ? std::string(err.localizedDescription.UTF8String) : "could not open the audio file";
      return out;
    }
    AVAudioFormat* target = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32 sampleRate:rate channels:1 interleaved:NO];
    AVAudioConverter* conv = [[AVAudioConverter alloc] initFromFormat:file.processingFormat toFormat:target];
    if (!conv) {
      if (error) *error = "this audio format can't be converted";
      return out;
    }
    const AVAudioFrameCount chunk = 32768;
    AVAudioPCMBuffer* in = [[AVAudioPCMBuffer alloc] initWithPCMFormat:file.processingFormat frameCapacity:chunk];
    __block bool done = false;
    __block NSError* readError = nil;
    while (true) {
      AVAudioPCMBuffer* outBuf = [[AVAudioPCMBuffer alloc] initWithPCMFormat:target frameCapacity:chunk];
      NSError* convError = nil;
      AVAudioConverterOutputStatus status = [conv convertToBuffer:outBuf
                                                           error:&convError
                                              withInputFromBlock:^AVAudioBuffer*(AVAudioPacketCount, AVAudioConverterInputStatus* inStatus) {
                                                if (done) {
                                                  *inStatus = AVAudioConverterInputStatus_EndOfStream;
                                                  return nil;
                                                }
                                                NSError* e = nil;
                                                if (![file readIntoBuffer:in frameCount:chunk error:&e] || in.frameLength == 0) {
                                                  readError = e;
                                                  done = true;
                                                  *inStatus = AVAudioConverterInputStatus_EndOfStream;
                                                  return nil;
                                                }
                                                *inStatus = AVAudioConverterInputStatus_HaveData;
                                                return in;
                                              }];
      const float* data = outBuf.floatChannelData ? outBuf.floatChannelData[0] : nullptr;
      if (data) out.samples.insert(out.samples.end(), data, data + outBuf.frameLength);
      if (status == AVAudioConverterOutputStatus_Error) {
        if (error) *error = convError ? std::string(convError.localizedDescription.UTF8String) : "conversion failed";
        out.samples.clear();
        return out;
      }
      if (status == AVAudioConverterOutputStatus_EndOfStream || (done && outBuf.frameLength == 0)) break;
    }
    if (out.samples.empty() && error) *error = readError ? std::string(readError.localizedDescription.UTF8String) : "the file has no audio";
  }
  return out;
}

}  // namespace transcript
