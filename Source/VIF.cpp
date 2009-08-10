#include <assert.h>
#include "VIF.h"
#include "VPU.h"
#include "VPU1.h"
#include "GIF.h"
#include "Profiler.h"
#include "Ps2Const.h"
#include "RegisterStateFile.h"
#include "Log.h"

#ifdef	PROFILE
#define	PROFILE_VIFZONE "VIF"
#define PROFILE_VU1ZONE	"VU0"
#endif

#define LOG_NAME                ("vif")
#define STATE_REGS_XML          ("vif/regs.xml")
#define STATE_REGS_VPU_STAT     ("VPU_STAT")

using namespace Framework;
using namespace std;

//REMOVE
static int nExecTimes = 0;

CVIF::CVIF(CGIF& gif, uint8* ram, uint8* spr, const VPUINIT& vpu0Init, const VPUINIT& vpu1Init) :
m_gif(gif),
m_ram(ram),
m_spr(spr),
m_VPU_STAT(0),
m_enabled(true)
{
    m_pVPU[0] = new CVPU(*this, 0, vpu0Init);
    m_pVPU[1] = new CVPU1(*this, 1, vpu1Init);
    for(unsigned int i = 0; i < 2; i++)
    {
        m_stream[i] = new CFifoStream(ram, spr);
    }
    Reset();
}

CVIF::~CVIF()
{
    for(unsigned int i = 0; i < 2; i++)
    {
        delete m_pVPU[i];
        delete m_stream[i];
    }
}

void CVIF::Reset()
{
    for(unsigned int i = 0; i < 2; i++)
    {
        m_stream[i]->Flush();
        m_pVPU[i]->Reset();
    }
    m_VPU_STAT = 0;
    m_enabled = true;
}

void CVIF::SetEnabled(bool enabled)
{
    m_enabled = enabled;
}

void CVIF::SaveState(CZipArchiveWriter& archive)
{
    {
        CRegisterStateFile* registerFile = new CRegisterStateFile(STATE_REGS_XML);
        registerFile->SetRegister32(STATE_REGS_VPU_STAT, m_VPU_STAT);
        archive.InsertFile(registerFile);
    }

    m_pVPU[0]->SaveState(archive);
    m_pVPU[1]->SaveState(archive);
}

void CVIF::LoadState(CZipArchiveReader& archive)
{
    CRegisterStateFile registerFile(*archive.BeginReadFile(STATE_REGS_XML));
    m_VPU_STAT = registerFile.GetRegister32(STATE_REGS_VPU_STAT);

    m_pVPU[0]->LoadState(archive);
    m_pVPU[1]->LoadState(archive);
}

uint32 CVIF::ReceiveDMA0(uint32 address, uint32 qwc, bool tagIncluded)
{
    unsigned int vpuNumber = 0;
	assert(0);
//    if(IsVU1Running())
//    {
//        thread::yield();
//        return 0;
//    }

 //   m_stream[vpuNumber]->SetDmaParams(address, qwc * 0x10);
 //   if(tagIncluded)
 //   {
 //       m_stream[vpuNumber]->Read(NULL, 8);
 //   }

 //   m_pVPU[vpuNumber]->ProcessPacket(*m_stream[vpuNumber]);

 //   uint32 remainingSize = m_stream[vpuNumber]->GetSize();
 //   assert((remainingSize & 0x0F) == 0);
 //   remainingSize /= 0x10;
	//return qwc - remainingSize;

    return qwc;
}

uint32 CVIF::ReceiveDMA1(uint32 nAddress, uint32 nQWC, bool nTagIncluded)
{
    if(!m_enabled)
    {
        return nQWC;
    }

    unsigned int vpuNumber = 1;

    if(IsVu1Running() && IsVu1WaitingForProgramEnd())
    {
        return 0;
    }

//    if(IsVU1Running())
//    {
//        thread::yield();
//        return 0;
//    }

#ifdef _DEBUG
    CLog::GetInstance().Print(LOG_NAME, "vif%i : Processing packet @ 0x%0.8X, qwc = %X, tagIncluded = %i\r\n",
        vpuNumber, nAddress, nQWC, static_cast<int>(nTagIncluded));
#endif

#ifdef PROFILE
	CProfiler::GetInstance().BeginZone(PROFILE_VIFZONE);
#endif
/*
    uint32 availableSpace = m_pVPU[vpuNumber]->m_cmdBuffer.GetAvailableWriteBytes() & ~0x0F;
    uint32 size = nQWC * 0x10;
    size = min<uint32>(availableSpace, size);
    if(size != 0)
    {
        if(nTagIncluded)
        {
            nAddress += 8;
            size -= 8;
        }
//        size_t written = m_pVPU[vpuNumber]->m_cmdBuffer.Write(m_ram + nAddress, size);
        static uint8 currentValue = 0;
        uint8* tempData = reinterpret_cast<uint8*>(alloca(size));
        for(unsigned int i = 0; i < size; i++)
        {
            tempData[i] = currentValue;
            currentValue++;
        }
        size_t written = m_pVPU[vpuNumber]->m_cmdBuffer.Write(tempData, size);
        assert(written == size);
    }

    uint32 writtenSize = (size + 0x0F) / 0x10;
    return writtenSize;
*/

    m_stream[vpuNumber]->SetDmaParams(nAddress, nQWC * 0x10);
    if(nTagIncluded)
    {
        m_stream[vpuNumber]->Read(NULL, 8);
    }

    m_pVPU[vpuNumber]->ProcessPacket(*m_stream[vpuNumber]);

#ifdef PROFILE
	CProfiler::GetInstance().EndZone();
#endif

    uint32 remainingSize = m_stream[vpuNumber]->GetAvailableReadQwords();
    assert((remainingSize & 0x0F) == 0);
    remainingSize /= 0x10;

	return nQWC - remainingSize;
}

uint32 CVIF::GetITop0() const
{
    return m_pVPU[0]->GetITOP();
}

uint32 CVIF::GetITop1() const
{
    return m_pVPU[1]->GetITOP();
}

uint32 CVIF::GetTop1() const
{
	return m_pVPU[1]->GetTOP();
}

void CVIF::StopVU(CMIPS* pCtx)
{
    if(pCtx == &m_pVPU[1]->GetContext())
    {
		m_VPU_STAT &= ~STAT_VBS1;
    }
}

void CVIF::ProcessXGKICK(uint32 nAddress)
{
	nAddress &= 0x3FF;
	nAddress *= 0x10;

//    assert(nAddress < PS2::VUMEM1SIZE);

	m_gif.ProcessPacket(m_pVPU[1]->GetVuMemory(), nAddress, PS2::VUMEM1SIZE);
}

bool CVIF::IsVu0Running() const
{
    return m_pVPU[0]->IsRunning();
}

bool CVIF::IsVu1Running() const
{
	return m_pVPU[1]->IsRunning();
}

bool CVIF::IsVu0WaitingForProgramEnd() const
{
    return m_pVPU[0]->IsWaitingForProgramEnd();
}

bool CVIF::IsVu1WaitingForProgramEnd() const
{
    return m_pVPU[1]->IsWaitingForProgramEnd();
}

void CVIF::ExecuteVu0(bool singleStep)
{
	assert(0);
}

void CVIF::ExecuteVu1(bool singleStep)
{
	if(!IsVu1Running()) return;
	m_pVPU[1]->Execute(singleStep);
}

bool CVIF::MustVu1Break() const
{
	if(!IsVu1Running()) return false;
	return m_pVPU[1]->MustBreak();
}

uint8* CVIF::GetRam() const
{
    return m_ram;
}

CGIF& CVIF::GetGif() const
{
    return m_gif;
}

uint32 CVIF::GetStat() const
{
    return m_VPU_STAT;
}

void CVIF::SetStat(uint32 stat)
{
    m_VPU_STAT = stat;
}


CVIF::CFifoStream::CFifoStream(uint8* ram, uint8* spr) :
m_ram(ram),
m_spr(spr),
m_source(NULL),
m_address(0),
m_endAddress(0),
m_nextAddress(0),
m_position(BUFFERSIZE)
{

}

CVIF::CFifoStream::~CFifoStream()
{

}

void CVIF::CFifoStream::Read(void* buffer, uint32 size)
{
    assert(m_source != NULL);
    uint8* readBuffer = reinterpret_cast<uint8*>(buffer);
    while(size != 0)
    {
        SyncBuffer();
        uint32 read = min<uint32>(size, BUFFERSIZE - m_position);
        if(readBuffer != NULL)
        {
            memcpy(readBuffer, reinterpret_cast<uint8*>(&m_buffer) + m_position, read);
            readBuffer += read;
        }
        m_position += read;
        size -= read;
    }
}

void CVIF::CFifoStream::Flush()
{
    m_position = BUFFERSIZE;
}

void CVIF::CFifoStream::SetDmaParams(uint32 address, uint32 qwc)
{
    if(address & 0x80000000)
    {
        m_source = m_spr;
        m_address = address & (PS2::SPRSIZE - 1);
        assert((m_address + qwc) <= PS2::SPRSIZE);
    }
    else
    {
        m_source = m_ram;
        m_address = address & (PS2::EERAMSIZE - 1);
    }
    m_nextAddress = m_address;
    m_endAddress = m_address + qwc;
    SyncBuffer();
}

uint32 CVIF::CFifoStream::GetAddress() const
{
	return m_address + m_position;
}

uint32 CVIF::CFifoStream::GetAvailableReadBytes() const
{
    return m_endAddress - GetAddress();
}

uint32 CVIF::CFifoStream::GetAvailableReadQwords() const
{
	return m_endAddress - m_nextAddress;
}

void CVIF::CFifoStream::Align32()
{
    unsigned int remainBytes = GetAddress() & 0x03;
    if(remainBytes == 0) return;
    Read(NULL, 4 - remainBytes);
    assert((GetAddress() & 0x03) == 0);
}

void CVIF::CFifoStream::SyncBuffer()
{
    if(m_position >= BUFFERSIZE)
    {
        if(m_nextAddress >= m_endAddress)
        {
            throw exception();
        }
        m_address = m_nextAddress;
        m_buffer = *reinterpret_cast<uint128*>(&m_source[m_nextAddress]);
        m_nextAddress += 0x10;
        m_position = 0;
    }
}
