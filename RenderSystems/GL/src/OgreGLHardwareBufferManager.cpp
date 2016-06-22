/*
-----------------------------------------------------------------------------
This source file is part of OGRE
    (Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2013 Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/
#include "OgreGLHardwareBufferManager.h"
#include "OgreGLHardwareVertexBuffer.h"
#include "OgreGLHardwareIndexBuffer.h"
#include "OgreGLRenderSystem.h"
#include "OgreGLRenderToVertexBuffer.h"
#include "OgreGLUtil.h"
#include "OgreHardwareBuffer.h"
#include "OgreRoot.h"
#include "OgreRenderSystemCapabilities.h"

namespace Ogre {
    //-----------------------------------------------------------------------
	// Scratch pool management (32 bit structure)
	struct GLScratchBufferAlloc
	{
		/// Size in bytes
		uint32 size: 31;
		/// Free? (pack with size)
		uint32 free: 1;
	};
	#define SCRATCH_POOL_SIZE 1 * 1024 * 1024
	#define SCRATCH_ALIGNMENT 32
	//---------------------------------------------------------------------
    GLHardwareBufferManagerBase::GLHardwareBufferManagerBase() 
		: mScratchBufferPool(NULL), mMapBufferThreshold(OGRE_GL_DEFAULT_MAP_BUFFER_THRESHOLD)
    {
		mStateCacheManager = dynamic_cast<GLRenderSystem*>(Root::getSingleton().getRenderSystem())->getGLSupportRef()->getStateCacheManager();

        // Init scratch pool
        // TODO make it a configurable size?
        // 32-bit aligned buffer
        mScratchBufferPool = static_cast<char*>(OGRE_MALLOC_ALIGN(SCRATCH_POOL_SIZE, MEMCATEGORY_GEOMETRY, SCRATCH_ALIGNMENT));
        GLScratchBufferAlloc* ptrAlloc = (GLScratchBufferAlloc*)mScratchBufferPool;
        ptrAlloc->size = SCRATCH_POOL_SIZE;
        ptrAlloc->free = 1;

		// non-Win32 machines are having issues glBufferSubData, looks like buffer corruption
		// disable for now until we figure out where the problem lies			
#	if OGRE_PLATFORM != OGRE_PLATFORM_WIN32
		mMapBufferThreshold = 0;
#	endif

		// Win32 machines with ATI GPU are having issues glMapBuffer, looks like buffer corruption
		// disable for now until we figure out where the problem lies			
#	if OGRE_PLATFORM == OGRE_PLATFORM_WIN32
		if (Root::getSingleton().getRenderSystem()->getCapabilities()->getVendor() == GPU_AMD)
		{
			mMapBufferThreshold = 0xffffffffUL  /* maximum unsigned long value */;
		}
#	endif

    }
    //-----------------------------------------------------------------------
    GLHardwareBufferManagerBase::~GLHardwareBufferManagerBase()
    {
        destroyAllDeclarations();
        destroyAllBindings();

		OGRE_FREE_ALIGN(mScratchBufferPool, MEMCATEGORY_GEOMETRY, SCRATCH_ALIGNMENT);
    }
    //-----------------------------------------------------------------------
    HardwareVertexBufferSharedPtr GLHardwareBufferManagerBase::createVertexBuffer(
        size_t vertexSize, size_t numVerts, HardwareBuffer::Usage usage, bool useShadowBuffer)
    {
		GLHardwareVertexBuffer* buf = 
			new GLHardwareVertexBuffer(this, vertexSize, numVerts, usage, useShadowBuffer);
		{
                    OGRE_LOCK_MUTEX(mVertexBuffersMutex);
			mVertexBuffers.insert(buf);
		}
		return HardwareVertexBufferSharedPtr(buf);
    }
    //-----------------------------------------------------------------------
    HardwareIndexBufferSharedPtr 
    GLHardwareBufferManagerBase::createIndexBuffer(
        HardwareIndexBuffer::IndexType itype, size_t numIndexes, 
        HardwareBuffer::Usage usage, bool useShadowBuffer)
    {
		GLHardwareIndexBuffer* buf = 
			new GLHardwareIndexBuffer(this, itype, numIndexes, usage, useShadowBuffer);
		{
                    OGRE_LOCK_MUTEX(mIndexBuffersMutex);
			mIndexBuffers.insert(buf);
		}
		return HardwareIndexBufferSharedPtr(buf);
    }
    //---------------------------------------------------------------------
    RenderToVertexBufferSharedPtr 
        GLHardwareBufferManagerBase::createRenderToVertexBuffer()
	{
        return RenderToVertexBufferSharedPtr(new GLRenderToVertexBuffer);
    }
	//---------------------------------------------------------------------
	HardwareUniformBufferSharedPtr 
		GLHardwareBufferManagerBase::createUniformBuffer(size_t sizeBytes, HardwareBuffer::Usage usage,bool useShadowBuffer, const String& name)
	{
		OGRE_EXCEPT(Exception::ERR_RENDERINGAPI_ERROR, 
                    "Uniform buffers not supported in OpenGL RenderSystem.",
                    "GLHardwareBufferManagerBase::createUniformBuffer");
	}
    HardwareCounterBufferSharedPtr
        GLHardwareBufferManagerBase::createCounterBuffer(size_t sizeBytes,
                                                         HardwareBuffer::Usage usage,
                                                         bool useShadowBuffer, const String& name)
	{
		OGRE_EXCEPT(Exception::ERR_RENDERINGAPI_ERROR,
                    "Counter buffers not supported in OpenGL RenderSystem.",
                    "GLHardwareBufferManagerBase::createCounterBuffer");
	}

    //---------------------------------------------------------------------
    GLenum GLHardwareBufferManagerBase::getGLUsage(unsigned int usage)
    {
        switch(usage)
        {
        case HardwareBuffer::HBU_STATIC:
        case HardwareBuffer::HBU_STATIC_WRITE_ONLY:
            return GL_STATIC_DRAW_ARB;
        case HardwareBuffer::HBU_DYNAMIC:
        case HardwareBuffer::HBU_DYNAMIC_WRITE_ONLY:
            return GL_DYNAMIC_DRAW_ARB;
        case HardwareBuffer::HBU_DYNAMIC_WRITE_ONLY_DISCARDABLE:
            return GL_STREAM_DRAW_ARB;
        default:
            return GL_DYNAMIC_DRAW_ARB;
        };
    }
    //---------------------------------------------------------------------
    GLenum GLHardwareBufferManagerBase::getGLType(unsigned int type)
    {
        switch(type)
        {
            case VET_FLOAT1:
            case VET_FLOAT2:
            case VET_FLOAT3:
            case VET_FLOAT4:
                return GL_FLOAT;
            case VET_SHORT1:
            case VET_SHORT2:
            case VET_SHORT3:
            case VET_SHORT4:
                return GL_SHORT;
            case VET_COLOUR:
			case VET_COLOUR_ABGR:
			case VET_COLOUR_ARGB:
            case VET_UBYTE4:
                return GL_UNSIGNED_BYTE;
            default:
                return 0;
        };
    }
	//---------------------------------------------------------------------
	//---------------------------------------------------------------------
	void* GLHardwareBufferManagerBase::allocateScratch(uint32 size)
	{
		// simple forward link search based on alloc sizes
		// not that fast but the list should never get that long since not many
		// locks at once (hopefully)
            OGRE_LOCK_MUTEX(mScratchMutex);


        // Alignment - round up the size to OGRE_SIMD_ALIGNMENT bits
        // control blocks are 32 bits which should give enough space.
        size += 2*OGRE_SIMD_ALIGNMENT - (size % OGRE_SIMD_ALIGNMENT);

        uint32 bufferPos = 0;
        while (bufferPos < SCRATCH_POOL_SIZE)
        {
            GLScratchBufferAlloc* pNext = (GLScratchBufferAlloc*)(mScratchBufferPool + bufferPos);
            // Big enough?
            if (pNext->free && pNext->size >= size)
            {
                uint32 offset = size;

                GLScratchBufferAlloc* pSplitAlloc = (GLScratchBufferAlloc*)
                    (mScratchBufferPool + bufferPos + offset);
                pSplitAlloc->free = 1;
                // split size is remainder minus new control block
                pSplitAlloc->size = pNext->size - size;

                // New size of current
                pNext->size = size;
                // allocate and return
                pNext->free = 0;

                // return pointer just after this control block that is also multiple of alignment
                return (char*)(pNext) + OGRE_SIMD_ALIGNMENT;
            }

            bufferPos += pNext->size;

		}

		// no available alloc
		return 0;

	}
	//---------------------------------------------------------------------
	void GLHardwareBufferManagerBase::deallocateScratch(void* ptr)
	{
            OGRE_LOCK_MUTEX(mScratchMutex);

        // Simple linear search dealloc
        uint32 bufferPos = 0;
        GLScratchBufferAlloc* pLast = 0;
        while (bufferPos < SCRATCH_POOL_SIZE)
        {
            GLScratchBufferAlloc* pCurrent = (GLScratchBufferAlloc*)(mScratchBufferPool + bufferPos);

            // Pointers match?
            if ((mScratchBufferPool + bufferPos + OGRE_SIMD_ALIGNMENT)
                == ptr)
            {
                // dealloc
                pCurrent->free = 1;
                
                // merge with previous
                if (pLast && pLast->free)
                {
                    // adjust buffer pos
                    bufferPos -= pLast->size;
                    // merge free space
                    pLast->size += pCurrent->size;
                    pCurrent = pLast;
                }

                // merge with next
                uint32 offset = bufferPos + pCurrent->size;
                if (offset < SCRATCH_POOL_SIZE)
                {
                    GLScratchBufferAlloc* pNext = (GLScratchBufferAlloc*)(
                        mScratchBufferPool + offset);
                    if (pNext->free)
                    {
                        pCurrent->size += pNext->size;
                    }
                }

				// done
				return;
			}

            bufferPos += pCurrent->size;
            pLast = pCurrent;

		}

		// Should never get here unless there's a corruption
		assert (false && "Memory deallocation error");


	}
	//---------------------------------------------------------------------
	size_t GLHardwareBufferManagerBase::getGLMapBufferThreshold() const
	{
		return mMapBufferThreshold;
	}
	//---------------------------------------------------------------------
	void GLHardwareBufferManagerBase::setGLMapBufferThreshold( const size_t value )
	{
		mMapBufferThreshold = value;
	}
}