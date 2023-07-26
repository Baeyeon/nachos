// filehdr.cc 
//	Routines for managing the disk file header (in UNIX, this
//	would be called the i-node).
//
//	The file header is used to locate where on disk the 
//	file's data is stored.  We implement this as a fixed size
//	table of pointers -- each entry in the table points to the 
//	disk sector containing that portion of the file data
//	(in other words, there are no indirect or doubly indirect 
//	blocks). The table size is chosen so that the file header
//	will be just big enough to fit in one disk sector, 
//
//      Unlike in a real system, we do not keep track of file permissions, 
//	ownership, last modification date, etc., in the file header. 
//
//	A file header can be initialized in two ways:
//	   for a new file, by modifying the in-memory data structure
//	     to point to the newly allocated data blocks
//	   for a file already on disk, by reading the file header from disk
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"

#include "system.h"
#include "filehdr.h"

//----------------------------------------------------------------------
// FileHeader::Allocate
// 	Initialize a fresh file header for a newly created file.
//	Allocate data blocks for the file out of the map of free disk blocks.
//	Return FALSE if there are not enough free blocks to accomodate
//	the new file.
//
//	"freeMap" is the bit map of free disk sectors
//	"fileSize" is the bit map of free disk sectors
//----------------------------------------------------------------------

bool
FileHeader::Allocate(BitMap *freeMap, int fileSize)
{ 
    numBytes = fileSize;  //�ֽ���
    numSectors  = divRoundUp(fileSize, SectorSize); //������
    if (freeMap->NumClear() < numSectors)   //ʣ������������
        return FALSE;		// not enough space
    else if ((NumDirect - 1) + NumDirect2 < numSectors) //numSectors>29+32������һ���ļ��������
        return false;
    else if (numSectors < NumDirect) //numSectors<30��ֻ����һ������
    {
        for (int i = 0; i < numSectors; i++)
        {
            dataSectors[i] = freeMap->Find();   //���������ռ�
            dataSectors[NumDirect-1] = -1;//dataSectrs[29] = -1; �ļ�ֻ���0-28��29��int�ֽڵ�������
        }
    }
    else {  //���ö�������
        for (int i = 0; i < NumDirect; i++)
            dataSectors[i] = freeMap->Find(); //���һ���Ƕ���������ĵ�ַ
        int dataSectors2[NumDirect2];
        for (int i = 0; i < numSectors - NumDirect + 1; i++) //��������-һ�������������29��������
            dataSectors2[i] = freeMap->Find();
        synchDisk->WriteSector(dataSectors[NumDirect - 1], (char*)dataSectors2);  //�����������
    }
    return TRUE;
}

//----------------------------------------------------------------------
// FileHeader::Deallocate
// 	De-allocate all the space allocated for data blocks for this file.
//
//	"freeMap" is the bit map of free disk sectors
//----------------------------------------------------------------------

void 
FileHeader::Deallocate(BitMap *freeMap)
{
    if (numSectors < NumDirect) {   //ֻ����һ������
        for (int i = 0; i < numSectors; i++) {
            ASSERT(freeMap->Test((int)dataSectors[i]));  // ought to be marked!
            freeMap->Clear((int)dataSectors[i]);    //�ͷŵ�һ��������
        }
    }
    else {   //���ö�������
        int dataSectors2[NumDirect2];
        synchDisk->ReadSector(dataSectors[NumDirect - 1], (char*)dataSectors2);
        //�ͷ�һ��������
        for (int i = 0; i < NumDirect; i++) {
            ASSERT(freeMap->Test((int)dataSectors[i]));  // ought to be marked!
            freeMap->Clear((int)dataSectors[i]);
        }
        //�ͷŶ���������
        for (int i = 0; i < numSectors - NumDirect + 1; i++) {
            ASSERT(freeMap->Test((int)dataSectors2[i]));  // ought to be marked!
            freeMap->Clear((int)dataSectors2[i]);
        }
    }
}

//----------------------------------------------------------------------
// FileHeader::FetchFrom
// 	Fetch contents of file header from disk. 
//
//	"sector" is the disk sector containing the file header
//----------------------------------------------------------------------

void
FileHeader::FetchFrom(int sector)
{
    synchDisk->ReadSector(sector, (char *)this);
}

//----------------------------------------------------------------------
// FileHeader::WriteBack
// 	Write the modified contents of the file header back to disk. 
//
//	"sector" is the disk sector to contain the file header
//----------------------------------------------------------------------

void
FileHeader::WriteBack(int sector)
{
    synchDisk->WriteSector(sector, (char *)this); 
}

//----------------------------------------------------------------------
// FileHeader::ByteToSector
// 	Return which disk sector is storing a particular byte within the file.
//      This is essentially a translation from a virtual address (the
//	offset in the file) to a physical address (the sector where the
//	data at the offset is stored).
//
//	"offset" is the location within the file of the byte in question
//----------------------------------------------------------------------

int
FileHeader::ByteToSector(int offset)
{
    int oriSector = offset / SectorSize;
    if (oriSector < NumDirect - 1)  //�����Ŵ���һ����������������
        return(dataSectors[oriSector]);
    else {  //�����Ŵ��ڶ���������������
        int dataSectors2[NumDirect2];
        synchDisk->ReadSector(dataSectors[NumDirect - 1], (char*)dataSectors2);
        return(dataSectors2[oriSector - NumDirect + 1]);
    }
}

//----------------------------------------------------------------------
// FileHeader::FileLength
// 	Return the number of bytes in the file.
//----------------------------------------------------------------------

int
FileHeader::FileLength()
{
    return numBytes;
}

//----------------------------------------------------------------------
// FileHeader::Print
// 	Print the contents of the file header, and the contents of all
//	the data blocks pointed to by the file header.
//----------------------------------------------------------------------

void
FileHeader::Print()
{
    int i, j, k;
    char *data = new char[SectorSize];
    //ֻ����һ������
    if (numSectors < NumDirect) {
        printf("FileHeader contents.  File size: %d.  File blocks:\n", numBytes);
        for (i = 0; i < numSectors; i++)
            printf("%d ", dataSectors[i]);
        printf("\nFile contents:\n");
        for (i = k = 0; i < numSectors; i++) {
            synchDisk->ReadSector(dataSectors[i], data);
            for (j = 0; (j < SectorSize) && (k < numBytes); j++, k++) {
                if ('\040' <= data[j] && data[j] <= '\176')   // isprint(data[j])
                    printf("%c", data[j]);
                else
                    printf("\\%x", (unsigned char)data[j]);
            }
            printf("\n");
        }
    }
    //���ö�������
    else {
        int dataSectors2[NumDirect2];
        synchDisk->ReadSector(dataSectors[NumDirect - 1], (char*)dataSectors2);
        printf("FileHeader contents.  File size: %d.  File blocks:\n", numBytes);
        //��ӡ������
        for (i = 0; i < NumDirect - 1; i++) //һ����������������
            printf("%d ", dataSectors[i]);
        for (i = 0; i < numSectors - NumDirect + 1; i++)    //������������������
            printf("%d ", dataSectors2[i]);
        //��ӡ�ļ�����
        printf("\nFile contents:\n");
        for (i = k = 0; i < NumDirect - 1; i++) {       //һ���������ļ����ݣ�0-28��������������
            synchDisk->ReadSector(dataSectors[i], data);
            for (j = 0; (j < SectorSize) && (k < numBytes); j++, k++) {
                if ('\040' <= data[j] && data[j] <= '\176')   // isprint(data[j])
                    printf("%c", data[j]);
                else
                    printf("\\%x", (unsigned char)data[j]);
            }
            printf("\n");
        }
        for (i = 0; i < numSectors - NumDirect + 1; i++) {  //�����������ļ����ݣ�ʣ��Ķ��������е�������������
            synchDisk->ReadSector(dataSectors2[i], data);
            for (j = 0; (j < SectorSize) && (k < numBytes); j++, k++) {
                if ('\040' <= data[j] && data[j] <= '\176')   // isprint(data[j])
                    printf("%c", data[j]);
                else
                    printf("\\%x", (unsigned char)data[j]);
            }
            printf("\n");
        }
    }
    delete [] data;
}

void
FileHeader::SetLength(int length)
{
    this->numBytes = length;
}

bool
FileHeader::ExtendSpace(BitMap* freeMap, int appendSize)
{
    int oriSectors = numSectors; //ԭ������
    numSectors = divRoundUp(appendSize, SectorSize) + oriSectors;	//��������
    //ʣ������������
    if (freeMap->NumClear() < numSectors - oriSectors) 
    {
        numSectors = oriSectors;
        return FALSE;		// not enough space
    }
    //�������������ļ����������
    if (numSectors > ((NumDirect - 1) + NumDirect2))
    {
        return false;
    }
    //�������ڴ�
    else {
        //ֻ��һ������
        if (numSectors < NumDirect) {
            for (int i = oriSectors; i < numSectors; i++) {
                dataSectors[i] = freeMap->Find();
                return true;
            }
        }
        //���ö�������
        else {
            //���δ���ö�������
            if (oriSectors < NumDirect) {
                for (int i = oriSectors; i < NumDirect; i++)
                {
                    dataSectors[i] = freeMap->Find();
                }
                int dataSectors2[NumDirect2];
                for (int i = 0; i < numSectors - NumDirect + 1; i++)
                    dataSectors2[i] = freeMap->Find();
                synchDisk->WriteSector(dataSectors[NumDirect - 1], (char*)dataSectors2);  //�����������
            }
            //������ö�������
            else {
                int dataSectors2[NumDirect2];
                synchDisk->ReadSector(dataSectors[NumDirect - 1], (char*)dataSectors2);
                for (int i = oriSectors - NumDirect + 1; i < numSectors - NumDirect + 1; i++)
                    dataSectors2[i] = freeMap->Find();      //�����������е��ļ���������д���Լ������չ����
                synchDisk->WriteSector(dataSectors[NumDirect - 1], (char*)dataSectors2);  //�����������
            }
            return true;
        }
    }
}
