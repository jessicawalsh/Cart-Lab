///////////////////////////////////////////////////////////////////////////////
//
//  File           : cart_driver.c
//  Description    : This is the implementation of the standardized IO functions
//                   for used to access the CART storage system.
//
//  Author         : Jessica Walsh
//  PSU email      : jaw6122@psu.edu
//

// Includes
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Project Includes
#include "cart_driver.h"
#include "cart_controller.h"


  // Implementation
  
  /*Create a struct to store frame and cart ID's*/
  struct frame_info{
    int cart_id;
    int frame_id;
  };

  /*Create a struct that stores information about each file*/
  struct file_info{
    char fileName[CART_MAX_PATH_LENGTH];
    int fileLength;
    int isOpen;
    uint32_t pos;
    int frame_num;
    struct frame_info fileFrame[100];
  };

  struct file_info file[CART_MAX_TOTAL_FILES];
  
  /*Variables to track next available frame and cart ID*/
  int nextCartID = 0;
  int nextFrameID = 0;

  /*variable used to determine if a file has already been initialized*/
  int initialize_status = 0;

  /*helper function to create a 64-bit opcode*/
  CartXferRegister createOpcode(CartXferRegister ky1, CartXferRegister ky2, CartXferRegister rt1, CartXferRegister ct1, CartXferRegister fm1)
  {
    CartXferRegister opCode;

    opCode = (ky1 << 56)| (ky2 << 48) | (rt1 << 47) | (ct1 << 31) | (fm1 << 15);

    return opCode;
  }  


////////////////////////////////////////////////////////////////////////////////
//
// Function     : cart_poweron
// Description  : Startup up the CART interface, initialize filesystem
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int32_t cart_poweron(void) {
  /*Declare Variables*/
  CartXferRegister ldcart;
  CartXferRegister bzero;
  CartXferRegister init;
  int i = 0;
  int x;
  
  /*Check if the filesystem has been initialized*/
  if(initialize_status == 0)
  {
    /*initialize the filesystem and change the status*/
    init = createOpcode(CART_OP_INITMS, 0, 0, 0, 0);
    cart_io_bus(init, NULL);
    initialize_status = 1;
  }

  
  while(i < CART_MAX_CARTRIDGES)
  {
    /*load cart*/
    ldcart = createOpcode(CART_OP_LDCART,0,0, i, 0);
    cart_io_bus(ldcart, NULL);  

    /*zero memory*/
    bzero =createOpcode(CART_OP_BZERO, 0, 0, 0, 0);
    cart_io_bus(bzero,NULL);

    i++;
  }

  /*initialize files*/
  for ( x = 0; x < CART_MAX_TOTAL_FILES; x++)
  {   
    strcpy(file[x].fileName,"");
    file[x].isOpen=0;
    file[x].pos = 0;
    file[x].fileLength = 0;
    file[x].frame_num = 0;
    file[x].fileFrame[0].cart_id = 0;
    file[x].fileFrame[0].frame_id = 0; 
  }


  /*Return successfully*/
  return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : cart_poweroff
// Description  : Shut down the CART interface, close all files
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int32_t cart_poweroff(void) {
  /*Declare variables*/
  CartXferRegister off;

  /*shut down the CART interface and close all files*/
  off = createOpcode(CART_OP_POWOFF, 0, 0, 0, 0);
  cart_io_bus(off, NULL); 

  /* Return successfully*/
  return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : cart_open
// Description  : This function opens the file and returns a file handle
//
// Inputs       : path - filename of the file to open
// Outputs      : file handle if successful, -1 if failure

int16_t cart_open(char *path) {
  int x = 0; 

  while (x < CART_MAX_TOTAL_FILES)
  {
    /*check if the file name is an empty string*/
    if((file[x].fileName[x]) == 0 )
    {
      /*copy the path to the unused file name*/
      strcpy(file[x].fileName, path);
      file[x].fileLength = 0;
      file[x].pos = 0;
      file[x].isOpen = 1;

      /*return the file handle*/
      return x; 
    }
    x++;
  }


  /*if the file does not exist create a new file*/
  strcpy(file[x].fileName, path);
  file[x].fileLength = 0;
  file[x].pos = 0;
  file[x].isOpen = 0;
  file[x].frame_num = 0;
  file[x].fileFrame[0].frame_id = nextFrameID ;
  file[x].fileFrame[0].cart_id = nextCartID;
  
  /*return the file handle*/
  return x; 
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : cart_close
// Description  : This function closes the file
//
// Inputs       : fd - the file descriptor
// Outputs      : 0 if successful, -1 if failure

int16_t cart_close(int16_t fd) {
  /*Check if the file is closed*/
  if(file[fd].isOpen == 0)
  {
    return -1;
  }
  else
  {
    /*if the file is open close it*/
    file[fd].isOpen = 0;
  }

  /*Return successfully*/
  return (0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : cart_read
// Description  : Reads "count" bytes from the file handle "fh" into the 
//                buffer "buf"
//
// Inputs       : fd - filename of the file to read from
//                buf - pointer to buffer to read into
//                count - number of bytes to read
// Outputs      : bytes read if successful, -1 if failure

int32_t cart_read(int16_t fd, void *buf, int32_t count) {
  /*Declare variables*/
  CartXferRegister ldcart;
  CartXferRegister read;
  char temp[CART_FRAME_SIZE];
  int32_t size;
  int frameID;
  int cartID;
  int index;
  int offset;
  int init = file[fd].fileLength - file[fd].pos;
  int offsetDiff;
  int remainingBytes;

  /*error checking*/
  if (file[fd].isOpen == 0)
  {
    return -1;
  }

  if(fd >= CART_MAX_TOTAL_FILES || fd < 0)
  {
    return -1;
  }

  if(count < 0)
  {
    return -1;
  }
  
  /*determine the remaining number of bytes to read*/ 
  if(count > init)
  {
    count = init;
  }

  remainingBytes = count;

  while(remainingBytes > 0)
  {
    /*calculate index to find cart and frame ID's*/    
    index = file[fd].pos / CART_MAX_TOTAL_FILES;

    /*calculate offset*/
    offset = file[fd].pos % CART_MAX_TOTAL_FILES;

    /*determine the size of bytes read*/
    offsetDiff = CART_FRAME_SIZE - offset;   
        
    if(offsetDiff < remainingBytes) 
    {
      size = offsetDiff;
    }
    else
    {
      size = remainingBytes;
    }

    frameID = file[fd].fileFrame[index].frame_id;
    cartID = file[fd].fileFrame[index].cart_id; 

    /*load cart*/
    ldcart = createOpcode(CART_OP_LDCART, 0, 0, cartID, 0);
    cart_io_bus(ldcart, buf);

    /*read the frame*/
    read = createOpcode(CART_OP_RDFRME, 0, 0,0, frameID);
    cart_io_bus(read, temp);

    /*copy temp into buf*/    
    memcpy(buf, temp + offset, size);

    /*update bytes left to read, position, and buffer*/
    remainingBytes -= size;
    file[fd].pos += size;   
    buf += size;      
  }

  /*return successfully*/
  return (count);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : cart_write
// Description  : Writes "count" bytes to the file handle "fh" from the 
//                buffer  "buf"
//
// Inputs       : fd - filename of the file to write to
//                buf - pointer to buffer to write from
//                count - number of bytes to write
// Outputs      : bytes written if successful, -1 if failure

int32_t cart_write(int16_t fd, void *buf, int32_t count) {
  /*Declare variables*/
  CartXferRegister ldcart;
  CartXferRegister rdfrme;
  CartXferRegister write;
  char temp[CART_FRAME_SIZE];
  int size;
  int index;
  int offset;
  int cartID;
  int frameID;
  int offsetDiff;
  int remainingBytes = count;

  /*error checking*/
  if (file[fd].isOpen == 0)
  {
    return -1;
  }

  if(fd >= CART_MAX_TOTAL_FILES || fd < 0)
  {
    return -1;
  }

  if(count < 0)
  {
    return -1;
  }

  while(remainingBytes > 0)
  {
    /*calculate index to find frame and cart ID's*/
    index= file[fd].pos / CART_MAX_TOTAL_FILES;

    /*calculate offset*/
    offset = file[fd].pos % CART_MAX_TOTAL_FILES;

    /*check if a new frame needs to be allocated*/  
    if(index >= file[fd].frame_num)
    {
      /*check if all cartridges have been used*/ 
      if(file[fd].fileFrame[index].cart_id > 63)
      {
        return -1;
      }
      /*update the cart, frame ID, and number of frames used*/
      file[fd].fileFrame[index].frame_id = nextFrameID;
      file[fd].fileFrame[index].cart_id = nextCartID;
      file[fd].frame_num++;

      if(nextFrameID == 1023)
      {
        nextCartID++;
        nextFrameID = 0;
      }
      else
      {
        nextFrameID++;
      }   
    }

    /*determine the size of bytes to write*/
    offsetDiff = CART_FRAME_SIZE - offset;     

    if(remainingBytes< offsetDiff)
    {
      size = remainingBytes;
    }
    else
    {
      size = offsetDiff;
    }

    cartID = file[fd].fileFrame[index].cart_id;
    frameID = file[fd].fileFrame[index].frame_id; 
    
    /*load cart*/
    ldcart = createOpcode(CART_OP_LDCART, 0, 0, cartID, 0);
    cart_io_bus(ldcart,buf);

    /*read frame*/
    rdfrme = createOpcode(CART_OP_RDFRME,0,0,0, frameID);
    cart_io_bus(rdfrme, temp); 
   
    /*copy buffer into temp*/
    memcpy(temp + offset, buf, size);

    /*write frame*/
    write = createOpcode(CART_OP_WRFRME, 0, 0, 0, frameID); 
    cart_io_bus(write, temp);

    /*update file position, remainingBytes and buf*/
    file[fd].pos += size; 
    remainingBytes -= size; 
    buf += size; 

    if (file[fd].pos > file[fd].fileLength)
    {
      file[fd].fileLength = file[fd].pos;
    }

 }
  
  /* Return successfully */
  return (count);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : cart_read
// Description  : Seek to specific point in the file
//
// Inputs       : fd - filename of the file to write to
//                loc - offfset of file in relation to beginning of file
// Outputs      : 0 if successful, -1 if failure

int32_t cart_seek(int16_t fd, uint32_t loc) {
  /*error checking*/
  if(file[fd].isOpen == 0)
  {
    return -1;
  }

  if(fd > CART_MAX_TOTAL_FILES || fd < 0)
  {
    return -1;
  }

  if( loc > file[fd].fileLength)
  {
    return -1;
  } 
  
  /*move file position to loc*/
  file[fd].pos =  loc; 

  /*Return successfully*/
  return (0);
}
