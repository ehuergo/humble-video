/*******************************************************************************
 * Copyright (c) 2014, Andrew "Art" Clarke.  All rights reserved.
 *   
 * This file is part of Humble-Video.
 *
 * Humble-Video is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Humble-Video is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Humble-Video.  If not, see <http://www.gnu.org/licenses/>.
 *******************************************************************************/
/*
 * EncoderTest.cpp
 *
 *  Created on: Sep 6, 2013
 *      Author: aclarke
 */
#include <io/humble/ferry/Logger.h>
#include <io/humble/ferry/LoggerStack.h>
#include <io/humble/ferry/RefPointer.h>
#include <io/humble/video/FilterGraph.h>
#include <io/humble/video/FilterPictureSink.h>
#include <io/humble/video/FilterAudioSink.h>
#include <io/humble/video/MediaPicture.h>
#include <io/humble/video/MediaAudio.h>
#include <io/humble/video/MediaAudioResampler.h>
#include "EncoderTest.h"

using namespace io::humble::ferry;
VS_LOG_SETUP(VS_CPP_PACKAGE.EncoderTest);

EncoderTest::EncoderTest() {
}

EncoderTest::~EncoderTest() {
}

void
EncoderTest::testCreation() {
  Logger::setGlobalIsLogging(Logger::LEVEL_TRACE, false);

  RefPointer<Codec> codec = Codec::findEncodingCodec(Codec::CODEC_ID_H264);
  RefPointer<Encoder> encoder = Encoder::make(codec.value());
  TS_ASSERT(encoder);
}

void
EncoderTest::testEncodeVideo() {
  Logger::setGlobalIsLogging(Logger::LEVEL_TRACE, false);
  LoggerStack stack;
  stack.setGlobalLevel(Logger::LEVEL_INFO, false);

  const bool isMemCheck = getenv("VS_TEST_MEMCHECK") ? true : false;
  const int32_t maxPics = isMemCheck ? 10 : 500;
  int32_t width=176;
  int32_t height=144;

  RefPointer<Codec> codec = Codec::findEncodingCodec(Codec::CODEC_ID_H264);
  RefPointer<Encoder> encoder = Encoder::make(codec.value());
  RefPointer<MediaPicture> picture = MediaPicture::make(width*2,height*2,
      PixelFormat::PIX_FMT_YUV420P);

  // set the encoder properties we need
  encoder->setWidth(picture->getWidth());
  encoder->setHeight(picture->getHeight());
  encoder->setPixelFormat(picture->getFormat());
  encoder->setProperty("b", (int64_t)400000); // bitrate
  encoder->setProperty("g", (int64_t) 10); // gop
  encoder->setProperty("bf", (int64_t)1); // max b frames

  RefPointer<Rational> tb = Rational::make(1,25);
  encoder->setTimeBase(tb.value());

  // mandlebrot, that is then negated, horizontally flipped, and edge detected, before
  // final outputting to a new picture with each version in one of 4 quadrants.
  char graphCommand[1024];
  snprintf(graphCommand,sizeof(graphCommand),"mandelbrot=s=%dx%d[mb];"
      "[mb]split=4[0][1][2][3];"
      "[0]pad=iw*2:ih*2[a];"
      "[1]negate[b];"
      "[2]hflip[c];"
      "[3]edgedetect[d];"
      "[a][b]overlay=w[x];"
      "[x][c]overlay=0:h[y];"
      "[y][d]overlay=w:h[out]", width, height);

  RefPointer<FilterGraph> graph = FilterGraph::make();
  RefPointer<FilterPictureSink> fsink = graph->addPictureSink("out", picture->getFormat());
  graph->open(graphCommand);

  // let's set a frame time base of 1/30
  RefPointer<Rational> pictureTb = Rational::make(1,30);

  // create an output muxer
  RefPointer<Muxer> muxer = Muxer::make("EncoderTest_encodeVideo.mov", 0, 0);
  RefPointer<MuxerFormat> format = muxer->getFormat();

  // if the container will require a global header, then the encoder needs to set this.
  if (format->getFlag(MuxerFormat::GLOBAL_HEADER))
    encoder->setFlag(Encoder::FLAG_GLOBAL_HEADER, true);

  // open the encoder
  encoder->open(0, 0);

  // add a stream for the encoded packets
  {
    RefPointer<MuxerStream> stream = muxer->addNewStream(encoder.value());
  }

  // and open the muxer
  muxer->open(0, 0);

  // now we're (in theory) ready to start writing data.
  int32_t numPics = 0;
  RefPointer<MediaPacket> packet;

  while(fsink->getPicture(picture.value()) >= 0 && numPics < maxPics) {
    picture->setTimeBase(pictureTb.value());
    picture->setTimeStamp(numPics);

    // let's encode
    packet = MediaPacket::make();
    encoder->encodeVideo(packet.value(), picture.value());
    if (packet->isComplete()) {
      muxer->write(packet.value(), false);
    }
    ++numPics;
  }
  // now flush the encoder
  do {
    packet = MediaPacket::make();
    encoder->encodeVideo(packet.value(), 0);
    if (packet->isComplete()) {
      muxer->write(packet.value(), false);
    }
  } while (packet->isComplete());

  muxer->close();

}

void
EncoderTest::testEncodeAudio() {
  Logger::setGlobalIsLogging(Logger::LEVEL_TRACE, false);
  LoggerStack stack;
  stack.setGlobalLevel(Logger::LEVEL_INFO, false);

  const bool isMemCheck = getenv("VS_TEST_MEMCHECK") ? true : false;
  const int32_t sampleRate = 44100;
  const int32_t maxSamples = isMemCheck ? sampleRate*0.5 : sampleRate*10;
  const int32_t numSamples = 1024;
  const AudioChannel::Layout channelLayout = AudioChannel::CH_LAYOUT_STEREO;
  const int32_t channels = AudioChannel::getNumChannelsInLayout(channelLayout);
  const AudioFormat::Type audioFormat = AudioFormat::SAMPLE_FMT_S16;
  RefPointer<Codec> codec = Codec::findEncodingCodec(Codec::CODEC_ID_AAC);
  RefPointer<Encoder> encoder = Encoder::make(codec.value());

  RefPointer<FilterGraph> graph = FilterGraph::make();

  RefPointer<MediaAudio> audio = MediaAudio::make(numSamples, sampleRate, channels, channelLayout,
      audioFormat);

  // set the encoder properties we need
  encoder->setSampleRate(audio->getSampleRate());
  encoder->setSampleFormat(audio->getFormat());
  encoder->setChannelLayout(audio->getChannelLayout());
  encoder->setChannels(audio->getChannels());
  encoder->setProperty("b", (int64_t)64000); // bitrate
  RefPointer<Rational> tb = Rational::make(1,25);
  encoder->setTimeBase(tb.value());

  // create an output muxer
  RefPointer<Muxer> muxer = Muxer::make("EncoderTest_encodeAudio.mp4", 0, 0);
  RefPointer<MuxerFormat> format = muxer->getFormat();
  if (format->getFlag(MuxerFormat::GLOBAL_HEADER))
    encoder->setFlag(Encoder::FLAG_GLOBAL_HEADER, true);

  // open the encoder
  encoder->open(0, 0);

  RefPointer<FilterAudioSink> fsink = graph->addAudioSink("out", audio->getSampleRate(), audio->getChannelLayout(), audio->getFormat());

  // Generate a 220 Hz sine wave with a 880 Hz beep each second, for 10 seconds.
  graph->open("sine=frequency=660:beep_factor=4:duration=11[out]");
  // Generate an amplitude modulated signal
  //graph->open("aevalsrc=sin(10*2*PI*t)*sin(880*2*PI*t)[out]");

  // add a stream for the encoded packets
  {
    RefPointer<MuxerStream> stream = muxer->addNewStream(encoder.value());
  }

  // and open the muxer
  muxer->open(0, 0);

  // now we're (in theory) ready to start writing data.
  int32_t numFrames = 0;
  RefPointer<MediaPacket> packet;

  while(fsink->getAudio(audio.value()) >= 0 && audio->isComplete() && numFrames*audio->getNumSamples() < maxSamples) {
    audio->setTimeStamp(numFrames*audio->getNumSamples());

    // let's encode
    packet = MediaPacket::make();
    encoder->encodeAudio(packet.value(), audio.value());
    if (packet->isComplete()) {
      muxer->write(packet.value(), false);
    }
    ++numFrames;
  }
  // now flush the encoder
  do {
    packet = MediaPacket::make();
    encoder->encodeAudio(packet.value(), 0);
    if (packet->isComplete()) {
      muxer->write(packet.value(), false);
    }
  } while (packet->isComplete());

  muxer->close();
}

/**
 * Test that memory is not leaked/corrupted during error paths.
 */
void
EncoderTest::testEncodeInvalidParameters()
{
  // Sub-test 1
  {
    int32_t width=176;
    int32_t height=360; // invalid dimensions for H263 codec
    RefPointer<Codec> codec = Codec::findEncodingCodec(Codec::CODEC_ID_H263);
    RefPointer<Encoder> encoder = Encoder::make(codec.value());

    RefPointer<MediaPicture> picture = MediaPicture::make(width*2,height*2,
        PixelFormat::PIX_FMT_YUV420P);

    // set the encoder properties we need
    encoder->setWidth(picture->getWidth());
    encoder->setHeight(picture->getHeight());
    encoder->setPixelFormat(picture->getFormat());
    encoder->setProperty("b", (int64_t)400000); // bitrate
    encoder->setProperty("g", (int64_t) 10); // gop

    RefPointer<Rational> tb = Rational::make(1,25);
    encoder->setTimeBase(tb.value());

    // open the encoder
    try {
      // Temporarily turn down logging
      LoggerStack stack;
      stack.setGlobalLevel(Logger::LEVEL_ERROR, false);

      encoder->open(0, 0);
      TS_FAIL("should never get here");
    } catch (std::exception & e) {
      // ignore exception
    }
  }

  // Sub-test 2
  {
    int32_t width=176;
    int32_t height=144;

    RefPointer<Codec> codec = Codec::findEncodingCodec(Codec::CODEC_ID_H264);
    RefPointer<Encoder> encoder = Encoder::make(codec.value());
    RefPointer<MediaPicture> picture = MediaPicture::make(width*2,height*2,
        PixelFormat::PIX_FMT_YUV420P);

    // set the encoder properties we need
    encoder->setWidth(picture->getWidth());
    encoder->setHeight(picture->getHeight());
    encoder->setPixelFormat(picture->getFormat());
    encoder->setProperty("b", (int64_t)400000); // bitrate
    encoder->setProperty("g", (int64_t) 10); // gop
    encoder->setProperty("bf", (int64_t)1); // max b frames

    RefPointer<Rational> tb = Rational::make(1,25);
    encoder->setTimeBase(tb.value());

    // Do not open the encoder

    // create an output muxer
    RefPointer<Muxer> muxer = Muxer::make("EncoderTest_encodeVideo.mov", 0, 0);

    // add a stream for the encoded packets
    try {
      // Temporarily turn down logging
      LoggerStack stack;
      stack.setGlobalLevel(Logger::LEVEL_ERROR, false);

      RefPointer<MuxerStream> stream = muxer->addNewStream(encoder.value());
      TS_FAIL("should never get here");
    } catch (std::exception & e) {
      // success
    }
  }
}

void
EncoderTest::encodeAndMux(
    MediaSampled* media,
    Muxer* muxer,
    Encoder* encoder)
{
//  if (encoder->getCodecType() == MediaDescriptor::MEDIA_VIDEO)
//    // skip all video for now
//    return;
  RefPointer<MediaPacket> packet = MediaPacket::make();
  do {
    encoder->encode(packet.value(), media);
    if (packet->isComplete()) {
      muxer->write(packet.value(), true);
    }
  } while (!media && packet->isComplete()); // this forces flushing if media is null
}

void
EncoderTest::resampleEncodeAndMux(
    MediaSampled* input,
    MediaResampler* resampler,
    MediaSampled* output,
    Muxer* muxer,
    Encoder* encoder
    )
{
  do {
    if (resampler) {
      resampler->resample(output, input);
    }
    encodeAndMux(output, muxer, encoder);
  } while (!input && output->isComplete());
}

void
EncoderTest::decodeAndEncode(
    MediaPacket* packet,
    Decoder* decoder,
    MediaSampled* input,
    MediaResampler* resampler,
    MediaSampled* output,
    Muxer* muxer,
    Encoder* encoder
)
{
  int32_t offset = 0;
  int32_t bytesRead = 0;
  do {
    bytesRead += decoder->decode(input, packet, offset);
    if (input->isComplete()) {
      // we encode and write it
      resampleEncodeAndMux(input, resampler, output, muxer, encoder);
    }
    offset += bytesRead;
  } while ((!packet && input->isComplete()) || (packet && offset < packet->getSize()));
  if (!packet) {
    // we should also flush the encoders
    encodeAndMux(0, muxer, encoder);
  }
}

/**
 * This test will read ironman (FLV, H263 video and mp3 audio) and transcode
 * to MP4 (H264 Video and aac audio).
 */
void
EncoderTest::testTranscode()
{
  // enable trace logging
  Logger::setGlobalIsLogging(Logger::LEVEL_TRACE, false);
  const bool isMemCheck = getenv("VS_TEST_MEMCHECK") ? true : false;
  LoggerStack stack;
  stack.setGlobalLevel(Logger::LEVEL_INFO, false);

  TestData::Fixture* fixture;
  fixture=mFixtures.getFixture("testfile.flv");
//  fixture=mFixtures.getFixture("testfile_h264_mp4a_tmcd.mov");
//  fixture=mFixtures.getFixture("bigbuckbunny_h264_aac_5.1.mp4");
  TS_ASSERT(fixture);
  char filepath[2048];
  mFixtures.fillPath(fixture, filepath, sizeof(filepath));

  RefPointer<Demuxer> source = Demuxer::make();
  source->open(filepath, 0, false, true, 0, 0);
  int32_t numStreams = source->getNumStreams();
  TS_ASSERT_EQUALS(fixture->num_streams, numStreams);

  // Let's create a helper object to help us with decoding
  typedef struct {
    MediaDescriptor::Type type;
    RefPointer<DemuxerStream> stream;
    RefPointer<Decoder> decoder;
    RefPointer<MediaSampled> media;
  } DemuxerStreamHelper;

  // I know there are only 2 in the input file.
  DemuxerStreamHelper inputHelpers[10];
  for(int32_t i = 0; i < numStreams; i++) {
    DemuxerStreamHelper* input = &inputHelpers[i];
    input->stream = source->getStream(i);
    input->decoder = input->stream->getDecoder();
    if (!input->decoder)
      // skip
      break;
    input->decoder->open(0, 0);
    input->type = input->decoder->getCodecType();
    if (input->type == MediaDescriptor::MEDIA_AUDIO)
      input->media = MediaAudio::make(
          input->decoder->getFrameSize(),
          input->decoder->getSampleRate(),
          input->decoder->getChannels(),
          input->decoder->getChannelLayout(),
          input->decoder->getSampleFormat());
    else if (input->type  == MediaDescriptor::MEDIA_VIDEO)
      input->media = MediaPicture::make(
          input->decoder->getWidth(),
          input->decoder->getHeight(),
          input->decoder->getPixelFormat()
      );
  }

  // now, let's set up our output file.
  RefPointer<Muxer> muxer = Muxer::make("EncoderTest_testTranscode.mp4", 0, 0);
  RefPointer<MuxerFormat> format = muxer->getFormat();

  // Let's create a helper object to help us with decoding
  typedef struct {
    MediaDescriptor::Type type;
    RefPointer<MuxerStream> stream;
    RefPointer<MediaResampler> resampler;
    RefPointer<MediaSampled> media;
    RefPointer<Encoder> encoder;
  } MuxerStreamHelper;

  MuxerStreamHelper outputHelpers[10];
  for(int32_t i = 0; i < numStreams; i++) {
    DemuxerStreamHelper *input = &inputHelpers[i];
    MuxerStreamHelper *output = &outputHelpers[i];
    if (!input->decoder)
      // skip
      break;
    output->type = input->type;
    RefPointer<Encoder> encoder;
    if (output->type == MediaDescriptor::MEDIA_VIDEO) {
      RefPointer<Codec> codec = Codec::findEncodingCodec(Codec::CODEC_ID_H264);
      encoder = Encoder::make(codec.value());

      // set the encoder properties we need
      encoder->setWidth(input->decoder->getWidth());
      encoder->setHeight(input->decoder->getHeight());
      encoder->setPixelFormat(input->decoder->getPixelFormat());
      encoder->setProperty("b", (int64_t)400000); // bitrate
      encoder->setProperty("g", (int64_t) 10); // gop
      encoder->setProperty("bf", (int64_t)0); // max b frames
      RefPointer<Rational> tb = Rational::make(1,2997);
      encoder->setTimeBase(tb.value());


    } else if (output->type == MediaDescriptor::MEDIA_AUDIO) {
      RefPointer<Codec> codec = Codec::findEncodingCodec(Codec::CODEC_ID_AAC);
      encoder = Encoder::make(codec.value());

      // set the encoder properties we need
      encoder->setSampleRate(input->decoder->getSampleRate());
      encoder->setSampleFormat(input->decoder->getSampleFormat());
      encoder->setSampleFormat(AudioFormat::SAMPLE_FMT_S16);
      encoder->setChannelLayout(input->decoder->getChannelLayout());
      encoder->setChannels(input->decoder->getChannels());
      encoder->setProperty("b", (int64_t)64000); // bitrate
      RefPointer<Rational> tb = Rational::make(1,encoder->getSampleRate());
      encoder->setTimeBase(tb.value());
      //      //input->decoder->getTimeBase();
      //      output->encoder->setTimeBase(tb.value());

    }
    output->encoder.reset(encoder.value(), true);
    if (output->encoder) {
      if (format->getFlag(MuxerFormat::GLOBAL_HEADER))
        output->encoder->setFlag(Encoder::FLAG_GLOBAL_HEADER, true);

      output->encoder->open(0,0);

      // sometimes encoders need to change parameters to fit; let's see
      // if that happened.
      output->stream = muxer->addNewStream(output->encoder.value());
    }
    output->media = input->media;
    output->resampler = 0;
    if (output->type == MediaDescriptor::MEDIA_AUDIO) {
      // sometimes encoders only accept certain media types and discard
      // our suggestions. Let's check.
      if (
          output->encoder->getSampleRate() != input->decoder->getSampleRate() ||
          output->encoder->getSampleFormat() != input->decoder->getSampleFormat() ||
          output->encoder->getChannelLayout() != input->decoder->getChannelLayout() ||
          output->encoder->getChannels() != input->decoder->getChannels()
          )
      {
        // we need a resampler.
        VS_LOG_DEBUG("Resampling: [%"PRId32", %"PRId32", %"PRId32"] [%"PRId32", %"PRId32", %"PRId32"]",
                     (int32_t)output->encoder->getChannelLayout(),
                     (int32_t)output->encoder->getSampleRate(),
                     (int32_t)output->encoder->getSampleFormat(),
                     (int32_t)input->decoder->getChannelLayout(),
                     (int32_t)input->decoder->getSampleRate(),
                     (int32_t)input->decoder->getSampleFormat()
        );
        RefPointer<MediaAudioResampler> resampler = MediaAudioResampler::make(
            output->encoder->getChannelLayout(),
            output->encoder->getSampleRate(),
            output->encoder->getSampleFormat(),
            input->decoder->getChannelLayout(),
            input->decoder->getSampleRate(),
            input->decoder->getSampleFormat()
            );
        resampler->open();
        output->resampler.reset(resampler.value(), true);
        output->media = MediaAudio::make(
                  output->encoder->getFrameSize(),
                  output->encoder->getSampleRate(),
                  output->encoder->getChannels(),
                  output->encoder->getChannelLayout(),
                  output->encoder->getSampleFormat());
      }
    }
  }
  // now we should be ready to open the muxer
  muxer->open(0, 0);

  // now, let's start a decoding loop.
  RefPointer<MediaPacket> packet = MediaPacket::make();

  int numPackets = 0;
  while(source->read(packet.value()) >= 0) {
    // got a packet; now we try to decode it.
    if (packet->isComplete()) {
      int32_t streamNo = packet->getStreamIndex();
      DemuxerStreamHelper *input = &inputHelpers[streamNo];
      MuxerStreamHelper* output = &outputHelpers[streamNo];
      if (input->decoder) decodeAndEncode(
          packet.value(),
          input->decoder.value(),
          input->media.value(),
          output->resampler.value(),
          output->media.value(),
          muxer.value(),
          output->encoder.value());
      ++numPackets;
      if (isMemCheck && numPackets > 100) {
        VS_LOG_WARN("Exiting early under valgrind");
        break;
      }
    }
  }

  // now, flush any cached packets
  for(int i = 0; i < numStreams; i++) {
    DemuxerStreamHelper *input = &inputHelpers[i];
    MuxerStreamHelper* output = &outputHelpers[i];
    if (input->decoder) decodeAndEncode(
        0,
        input->decoder.value(),
        input->media.value(),
        output->resampler.value(),
        output->media.value(),
        muxer.value(),
        output->encoder.value());

  }
  source->close();
  muxer->close();
}

void
EncoderTest::testRegression36Internal (const Codec::ID codecId,
                                       const int32_t numSamples,
                                       const int32_t sampleRate,
                                       const int32_t channels,
                                       const AudioChannel::Layout channelLayout,
                                       const AudioFormat::Type audioFormat,
                                       const int64_t bitRate,
                                       const char* testOutputName)
{
  VS_LOG_DEBUG("Output filename: %s", testOutputName);
  RefPointer<Codec> codec = Codec::findEncodingCodec (codecId);
  RefPointer<Encoder> encoder = Encoder::make (codec.value ());
  RefPointer<FilterGraph> graph = FilterGraph::make ();
  RefPointer<MediaAudio> audio = MediaAudio::make (numSamples, sampleRate,
                                                   channels, channelLayout,
                                                   audioFormat);
  // set the encoder properties we need
  encoder->setSampleRate (audio->getSampleRate ());
  encoder->setSampleFormat (audio->getFormat ());
  encoder->setChannelLayout (audio->getChannelLayout ());
  encoder->setChannels (audio->getChannels ());
  encoder->setProperty ("b", (int64_t) (bitRate));
  RefPointer<Rational> tb = Rational::make (1, sampleRate);
  encoder->setTimeBase (tb.value ());
  // create an output muxer
  RefPointer<Muxer> muxer = Muxer::make (testOutputName, 0, 0);
  RefPointer<MuxerFormat> format = muxer->getFormat ();
  if (format->getFlag (MuxerFormat::GLOBAL_HEADER))
    encoder->setFlag (Encoder::FLAG_GLOBAL_HEADER, true);

  // open the encoder
  encoder->open (0, 0);
  RefPointer<FilterAudioSink> fsink = graph->addAudioSink (
      "out", audio->getSampleRate (), audio->getChannelLayout (),
      audio->getFormat ());
  // Generate a 220 Hz sine wave with a 880 Hz beep each second, for 10 seconds.
  graph->open ("sine=frequency=220:beep_factor=4:duration=11[out]");
  fsink->setFrameSize (numSamples);
  // add a stream for the encoded packets
  {
    RefPointer<MuxerStream> stream = muxer->addNewStream (encoder.value ());
  }      // and open the muxer
  muxer->open (0, 0);
  // now we're (in theory) ready to start writing data.
  int32_t numCompletePackets = 0;
  RefPointer<MediaPacket> packet;
  // Get one audio packet that is larger than the frame-size.
  fsink->getAudio (audio.value ());
  TS_ASSERT(audio->isComplete ());
  TS_ASSERT_EQUALS(audio->getNumSamples (), sampleRate);
  audio->setTimeStamp (0);
  // let's encode
  packet = MediaPacket::make ();
  encoder->encodeAudio (packet.value (), audio.value ());
  if (packet->isComplete ())
  {
    muxer->write (packet.value (), false);
  }
  // now flush the encoder
  do
  {
    packet = MediaPacket::make ();
    encoder->encodeAudio (packet.value (), 0);
    if (packet->isComplete ())
    {
      muxer->write (packet.value (), false);
      ++numCompletePackets;
    }
  }
  while (packet->isComplete ());
  muxer->close ();
  if (!(codec->getCapabilities() & Codec::CAP_VARIABLE_FRAME_SIZE)) {
    const int32_t numExpectedPackets = audio->getNumSamples()
        / encoder->getFrameSize();

    VS_LOG_DEBUG("%ld vs %ld; framesize: %ld", numCompletePackets, numExpectedPackets, encoder->getFrameSize());
    TS_ASSERT(numCompletePackets > 10);
  }
}

void
EncoderTest::testRegression36() {
  Logger::setGlobalIsLogging(Logger::LEVEL_TRACE, false);
  LoggerStack stack;
  stack.setGlobalLevel(Logger::LEVEL_INFO, false);

  {
    const int32_t sampleRate = 44100;
    const int32_t numSamples = sampleRate; // this is important for the test -- we want to extract 1 second of audio
    const AudioChannel::Layout channelLayout = AudioChannel::CH_LAYOUT_STEREO;
    const int32_t channels = AudioChannel::getNumChannelsInLayout(channelLayout);
    const AudioFormat::Type audioFormat = AudioFormat::SAMPLE_FMT_FLTP;
    const char* testOutputName = "EncoderTest_testRegression36.mp3";
    const Codec::ID codecId = Codec::CODEC_ID_MP3;
    const int64_t bitRate = 64000;

    testRegression36Internal (codecId, numSamples, sampleRate, channels,
                              channelLayout, audioFormat, bitRate,
                              testOutputName);
  }
  {
    const int32_t sampleRate = 22050;
    const int32_t numSamples = sampleRate; // this is important for the test -- we want to extract 1 second of audio
    const AudioChannel::Layout channelLayout = AudioChannel::CH_LAYOUT_MONO;
    const int32_t channels = AudioChannel::getNumChannelsInLayout(channelLayout);
    const AudioFormat::Type audioFormat = AudioFormat::SAMPLE_FMT_FLTP;
    const char* testOutputName = "EncoderTest_testRegression36.ogg";
    const Codec::ID codecId = Codec::CODEC_ID_VORBIS;
    const int64_t bitRate = 64000;

    testRegression36Internal (codecId, numSamples, sampleRate, channels,
                              channelLayout, audioFormat, bitRate,
                              testOutputName);
  }
  {
    const int32_t sampleRate = 22050;
    const int32_t numSamples = sampleRate; // this is important for the test -- we want to extract 1 second of audio
    const AudioChannel::Layout channelLayout = AudioChannel::CH_LAYOUT_MONO;
    const int32_t channels = AudioChannel::getNumChannelsInLayout(channelLayout);
    const AudioFormat::Type audioFormat = AudioFormat::SAMPLE_FMT_FLT;
    const char* testOutputName = "EncoderTest_testRegression36.flv";
    const Codec::ID codecId = Codec::CODEC_ID_NELLYMOSER;
    const int64_t bitRate = 64000;

    testRegression36Internal (codecId, numSamples, sampleRate, channels,
                              channelLayout, audioFormat, bitRate,
                              testOutputName);
  }
}
