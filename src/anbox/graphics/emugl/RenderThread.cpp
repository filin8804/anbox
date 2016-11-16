/*
* Copyright (C) 2011 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/
#include "RenderThread.h"

#include "ReadBuffer.h"
#include "RenderControl.h"
#include "RenderThreadInfo.h"
#include "Renderer.h"
#include "TimeUtils.h"

#include "../../../shared/OpenglCodecCommon/ChecksumCalculatorThreadInfo.h"
#include "OpenGLESDispatch/EGLDispatch.h"
#include "OpenGLESDispatch/GLESv1Dispatch.h"
#include "OpenGLESDispatch/GLESv2Dispatch.h"

#define STREAM_BUFFER_SIZE 4 * 1024 * 1024

RenderThread::RenderThread(IOStream *stream, emugl::Mutex *lock)
    : emugl::Thread(), m_lock(lock), m_stream(stream) {}

RenderThread::~RenderThread() {}

// static
RenderThread *RenderThread::create(IOStream *stream, emugl::Mutex *lock) {
  return new RenderThread(stream, lock);
}

void RenderThread::forceStop() { m_stream->forceStop(); }

intptr_t RenderThread::main() {
  RenderThreadInfo tInfo;
  ChecksumCalculatorThreadInfo tChecksumInfo;

  //
  // initialize decoders
  //
  tInfo.m_glDec.initGL(gles1_dispatch_get_proc_func, NULL);
  tInfo.m_gl2Dec.initGL(gles2_dispatch_get_proc_func, NULL);
  initRenderControlContext(&tInfo.m_rcDec);

  ReadBuffer readBuf(STREAM_BUFFER_SIZE);

  while (1) {
    int stat = readBuf.getData(m_stream);
    if (stat <= 0) {
      break;
    }

    bool progress;
    do {
      progress = false;

      m_lock->lock();
      //
      // try to process some of the command buffer using the GLESv1 decoder
      //
      size_t last =
          tInfo.m_glDec.decode(readBuf.buf(), readBuf.validData(), m_stream);
      if (last > 0) {
        progress = true;
        readBuf.consume(last);
      }

      //
      // try to process some of the command buffer using the GLESv2 decoder
      //
      last =
          tInfo.m_gl2Dec.decode(readBuf.buf(), readBuf.validData(), m_stream);
      if (last > 0) {
        progress = true;
        readBuf.consume(last);
      }

      //
      // try to process some of the command buffer using the
      // renderControl decoder
      //
      last = tInfo.m_rcDec.decode(readBuf.buf(), readBuf.validData(), m_stream);
      if (last > 0) {
        readBuf.consume(last);
        progress = true;
      }

      m_lock->unlock();

    } while (progress);
  }

  //
  // Release references to the current thread's context/surfaces if any
  //
  Renderer::get()->bindContext(0, 0, 0);
  if (tInfo.currContext || tInfo.currDrawSurf || tInfo.currReadSurf) {
    fprintf(stderr,
            "ERROR: RenderThread exiting with current context/surfaces\n");
  }

  Renderer::get()->drainWindowSurface();

  Renderer::get()->drainRenderContext();

  return 0;
}
