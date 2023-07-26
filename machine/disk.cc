// disk.cc 
//	Routines to simulate a physical disk device; reading and writing
//	to the disk is simulated as reading and writing to a UNIX file.
//	See disk.h for details about the behavior of disks (and
//	therefore about the behavior of this simulation).
//
//	Disk operations are asynchronous, so we have to invoke an interrupt
//	handler when the simulated operation completes.
//
//  DO NOT CHANGE -- part of the machine emulation
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "disk.h"
#include "system.h"

// We put this at the front of the UNIX file representing the
// disk, to make it less likely we will accidentally treat a useful file 
// as a disk (which would probably trash the file's contents).
#define MagicNumber 	0x456789ab  //
#define MagicSize 	sizeof(int)

#define DiskSize 	(MagicSize + (NumSectors * SectorSize)) //128K字节

// dummy procedure because we can't take a pointer of a member function
static void DiskDone(_int arg) { ((Disk *)arg)->HandleInterrupt(); } //cpp只允许一个成员函数指向一个类，不能指向其对象

//----------------------------------------------------------------------
// Disk::Disk()
// 	Initialize a simulated disk.  Open the UNIX file (creating it
//	if it doesn't exist), and check the magic number to make sure it's 
// 	ok to treat it as Nachos disk storage.
//
//	"name" -- text name of the file simulating the Nachos disk
//	"callWhenDone" -- interrupt handler to be called when disk read/write
//	   request completes
//	"callArg" -- argument to pass the interrupt handler
//----------------------------------------------------------------------

Disk::Disk(const char* name, VoidFunctionPtr callWhenDone, _int callArg)
{
    int magicNum;
    int tmp = 0;

    DEBUG('d', "Initializing the disk, 0x%x 0x%x\n", callWhenDone, callArg); 
    handler = callWhenDone; //中断处理程序
    handlerArg = callArg; //handler的参数
    lastSector = 0;
    bufferInit = 0;
    
    fileno = OpenForReadWrite((char*)name, FALSE); 
    if (fileno >= 0) {		 	// file exists, check magic number 
	Read(fileno, (char *) &magicNum, MagicSize);
	ASSERT(magicNum == MagicNumber);
    } else {				// file doesn't exist, create it
        fileno = OpenForWrite((char*)name);
	magicNum = MagicNumber;  
	WriteFile(fileno, (char *) &magicNum, MagicSize); // write magic number

	// need to write at end of file, so that reads will not return EOF
        Lseek(fileno, DiskSize - sizeof(int), 0);	 //DiskSize-4
	WriteFile(fileno, (char *)&tmp, sizeof(int));   //写完后有Disk映像
    }
    active = FALSE;
}

//----------------------------------------------------------------------
// Disk::~Disk()
// 	Clean up disk simulation, by closing the UNIX file representing the
//	disk.
//----------------------------------------------------------------------

Disk::~Disk()
{
    Close(fileno);
}

//----------------------------------------------------------------------
// Disk::PrintSector()
// 	Dump the data in a disk read/write request, for debugging.
//----------------------------------------------------------------------

static void
PrintSector (bool writing, int sector, char *data)
{
    int *p = (int *) data;

    if (writing)
        printf("Writing sector: %d\n", sector); 
    else
        printf("Reading sector: %d\n", sector); 
    for (unsigned int i = 0; i < (SectorSize/sizeof(int)); i++)
	printf("%x ", p[i]);
    printf("\n"); 
}

//----------------------------------------------------------------------
// Disk::ReadRequest/WriteRequest
// 	Simulate a request to read/write a single disk sector
//	   Do the read/write immediately to the UNIX file
//	   Set up an interrupt handler to be called later,
//	      that will notify the caller when the simulator says
//	      the operation has completed.
//
//	Note that a disk only allows an entire sector to be read/written,
//	not part of a sector.
//
//	"sectorNumber" -- the disk sector to read/write
//	"data" -- the bytes to be written, the buffer to hold the incoming bytes
//----------------------------------------------------------------------

void
Disk::ReadRequest(int sectorNumber, char* data)  
{
    int ticks = ComputeLatency(sectorNumber, FALSE); //计算从当前扇区号到目标扇区需多少时间

    ASSERT(!active);				// only one request at a time 
    ASSERT((sectorNumber >= 0) && (sectorNumber < NumSectors)); //检查扇区号是否在合法范围之内
    
    DEBUG('d', "Reading from sector %d\n", sectorNumber);
    Lseek(fileno, SectorSize * sectorNumber + MagicSize, 0); //0号扇区：128*0+4；1号扇区：128*1+4
    Read(fileno, data, SectorSize);
    if (DebugIsEnabled('d'))
	PrintSector(FALSE, sectorNumber, data);
    
    active = TRUE;  //设置当前扇区处于忙状态
    UpdateLast(sectorNumber);
    stats->numDiskReads++;
    interrupt->Schedule(DiskDone, (_int) this, ticks, DiskInt); //ticks时间后发生中断（磁盘操作完成），用DiskDone中断处理函数处理
}

void
Disk::WriteRequest(int sectorNumber, char* data)
{
    int ticks = ComputeLatency(sectorNumber, TRUE);

    ASSERT(!active);
    ASSERT((sectorNumber >= 0) && (sectorNumber < NumSectors));
    
    DEBUG('d', "Writing to sector %d\n", sectorNumber);
    Lseek(fileno, SectorSize * sectorNumber + MagicSize, 0);
    WriteFile(fileno, data, SectorSize);
    if (DebugIsEnabled('d'))
	PrintSector(TRUE, sectorNumber, data);
    
    active = TRUE;
    UpdateLast(sectorNumber);
    stats->numDiskWrites++;
    interrupt->Schedule(DiskDone, (_int) this, ticks, DiskInt);
}

//----------------------------------------------------------------------
// Disk::HandleInterrupt()
// 	Called when it is time to invoke the disk interrupt handler,
//	to tell the Nachos kernel that the disk request is done.
//----------------------------------------------------------------------

void
Disk::HandleInterrupt ()
{ 
    active = FALSE; //磁盘操作完成
    (*handler)(handlerArg); 
}

//----------------------------------------------------------------------
// Disk::TimeToSeek()
//	Returns how long it will take to position the disk head over the correct
//	track on the disk.  Since when we finish seeking, we are likely
//	to be in the middle of a sector that is rotating past the head,
//	we also return how long until the head is at the next sector boundary.
//	
//   	Disk seeks at one track per SeekTime ticks (cf. stats.h)
//   	and rotates at one sector per RotationTime ticks
//----------------------------------------------------------------------

int
Disk::TimeToSeek(int newSector, int *rotation)  //寻道
{
    int newTrack = newSector / SectorsPerTrack;
    int oldTrack = lastSector / SectorsPerTrack;
    int seek = abs(newTrack - oldTrack) * SeekTime;
				// how long will seek take?
    int over = (stats->totalTicks + seek) % RotationTime; 
				// will we be in the middle of a sector when
				// we finish the seek?

    *rotation = 0;
    if (over > 0)	 	// if so, need to round up to next full sector
   	*rotation = RotationTime - over;
    return seek;
}

//----------------------------------------------------------------------
// Disk::ModuloDiff()
// 	Return number of sectors of rotational delay between target sector
//	"to" and current sector position "from"
//----------------------------------------------------------------------

int 
Disk::ModuloDiff(int to, int from) //寻道
{
    int toOffset = to % SectorsPerTrack;
    int fromOffset = from % SectorsPerTrack;

    return ((toOffset - fromOffset) + SectorsPerTrack) % SectorsPerTrack;
}

//----------------------------------------------------------------------
// Disk::ComputeLatency()
// 	Return how long will it take to read/write a disk sector, from
//	the current position of the disk head.
//
//   	Latency = seek time + rotational latency + transfer time
//   	Disk seeks at one track per SeekTime ticks (cf. stats.h)
//   	and rotates at one sector per RotationTime ticks
//
//   	To find the rotational latency, we first must figure out where the 
//   	disk head will be after the seek (if any).  We then figure out
//   	how long it will take to rotate completely past newSector after 
//	that point.
//
//   	The disk also has a "track buffer"; the disk continuously reads
//   	the contents of the current disk track into the buffer.  This allows 
//   	read requests to the current track to be satisfied more quickly.
//   	The contents of the track buffer are discarded after every seek to 
//   	a new track.
//----------------------------------------------------------------------

int
Disk::ComputeLatency(int newSector, bool writing) //寻道
{
    int rotation;
    int seek = TimeToSeek(newSector, &rotation);
    int timeAfter = stats->totalTicks + seek + rotation;

#ifndef NOTRACKBUF	// turn this on if you don't want the track buffer stuff
    // check if track buffer applies
    if ((writing == FALSE) && (seek == 0) 
		&& (((timeAfter - bufferInit) / RotationTime) 
	     		> ModuloDiff(newSector, bufferInit / RotationTime))) {
        DEBUG('d', "Request latency = %d\n", RotationTime);
	return RotationTime; // time to transfer sector from the track buffer
    }
#endif

    rotation += ModuloDiff(newSector, timeAfter / RotationTime) * RotationTime;

    DEBUG('d', "Request latency = %d\n", seek + rotation + RotationTime);
    return(seek + rotation + RotationTime);
}

//----------------------------------------------------------------------
// Disk::UpdateLast
//   	Keep track of the most recently requested sector.  So we can know
//	what is in the track buffer.
//----------------------------------------------------------------------

void
Disk::UpdateLast(int newSector) //设置当前扇区为刚刚的扇区，为下次寻到时间做准备
{
    int rotate;
    int seek = TimeToSeek(newSector, &rotate);
    
    if (seek != 0)
	bufferInit = stats->totalTicks + seek + rotate;
    lastSector = newSector;
    DEBUG('d', "Updating last sector = %d, %d\n", lastSector, bufferInit);
}
