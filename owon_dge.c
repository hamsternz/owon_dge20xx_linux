/*
 * owon_dge2070 0.1 	A userspace USB driver that sends SCPI commands to the OWON
 *                     	DGE2070 signal generator.
 *
 *                      (c) 2025 Mike Field <hamster@snap.net.nz<
 *
 * Largely based on owondump by Michael Murphy <ee07m060@elec.qmul.ac.uk>
 * 
 * Commands are from https://files.owon.com.cn/software/Application/DGE2000_3000_SCPI_Protocol.pdf
 *
*/

#include <stdio.h>
#include <string.h>
#include <usb.h>

#define USB_LOCK_VENDOR 0x5345                    // Dev : (5345) Owon Technologies
#define USB_LOCK_PRODUCT 0x1234                   //       (1234) PDS Digital Oscilloscope
#define BULK_WRITE_ENDPOINT 0x01
#define BULK_READ_ENDPOINT 0x81
#define MAX_USB_LOCKS 10                                  // allow multiple scopes to slave to same PC host

#define DEFAULT_INTERFACE 0x00
#define DEFAULT_CONFIGURATION 0x01
#define DEFAULT_TIMEOUT 500                               // 500mS for USB timeouts

#define OWON_IDN_QUERY "*IDN?"
#define OWON_IDN_WANTED_35 "OWON,DGE2035,"
#define OWON_IDN_WANTED_70 "OWON,DGE2070,"



struct usb_device *usb_locks[MAX_USB_LOCKS];
int locksFound = 0;
int verbose = 0;
int model = 0;

// add the device locks to an array.
void found_usb_lock(struct usb_device *dev) {
  if(locksFound < MAX_USB_LOCKS) {
    usb_locks[locksFound++] = dev;
  }
}


struct usb_device *devfindOwon(void) {
    struct usb_bus *bus;
    struct usb_dev_handle *dh;
   
    usb_find_busses();
    usb_find_devices();

    for (bus = usb_busses; bus; bus = bus->next) {
        struct usb_device *dev;
        for (dev = bus->devices; dev; dev = dev->next) {
	      if(dev->descriptor.idVendor == USB_LOCK_VENDOR && dev->descriptor.idProduct == USB_LOCK_PRODUCT) {
	        found_usb_lock(dev);
	        if(verbose) printf("..Found an Owon device %04x:%04x on bus %s\n", USB_LOCK_VENDOR,USB_LOCK_PRODUCT, bus->dirname);
	    	dh=usb_open(dev);
	       	usb_reset(dh); // quirky.. device has to be initially reset
	    	return(dev);   // return the device
	      }
	  }
    }
    return (NULL);
}


int send_query(usb_dev_handle * devHandle, char *cmd, int cmd_len, char *buf, int buf_len) {
    signed int ret=0;	// set to < 0 to indicate USB errors
    ret = usb_clear_halt(devHandle, BULK_WRITE_ENDPOINT);

    if(verbose) printf("..Attempting to bulk write '%s' command to device...\n", cmd);
    ret = usb_bulk_write(devHandle, BULK_WRITE_ENDPOINT, cmd, cmd_len, DEFAULT_TIMEOUT);

    if(ret < 0) {
	printf("..Failed to bulk write %04x '%s'\n", ret, strerror(-ret));
	return -1;
    }
    if(verbose) printf("..Successful bulk write of %04x bytes!\n", cmd_len);

    if(buf == NULL) {
        return 0;
    }

    // clear any halt status on the bulk IN endpoint
    ret = usb_clear_halt(devHandle, BULK_READ_ENDPOINT);

    if(verbose) printf("..Attempting to bulk read %04x (%d) bytes from device...\n",(unsigned int) buf_len, (unsigned int)  buf_len);
    ret = usb_bulk_read(devHandle, BULK_READ_ENDPOINT, buf, buf_len, DEFAULT_TIMEOUT);

    if(ret < 0) {
    	usb_resetep(devHandle,BULK_READ_ENDPOINT);
    	printf("..Failed to bulk read: %04x (%d) bytes: '%s'\n", (unsigned int) buf_len,(unsigned int)  buf_len, strerror(-ret));
      	return -1;
    }
    if(verbose) printf("..Successful bulk read of %04x (%d) bytes! :\n", ret, ret);

    return ret;
}


usb_dev_handle *connect_to_device(struct usb_device *dev) {
    usb_dev_handle *devHandle = 0;

    unsigned int ret=0;	// set to < 0 to indicate USB errors
    
    char owonDescriptorBuffer[0x12];

    if(dev->descriptor.idVendor != USB_LOCK_VENDOR || dev->descriptor.idProduct != USB_LOCK_PRODUCT) {
        printf("..Failed device lock attempt: not passed a USB device handle!\n");
        return NULL;
    }

    devHandle = usb_open(dev);

    if(devHandle == NULL) {
      printf("..Failed to open device..\'%s\'", strerror(-ret));
      return NULL;
    }

    ret = usb_set_configuration(devHandle, DEFAULT_CONFIGURATION);
    ret = usb_claim_interface(devHandle, DEFAULT_INTERFACE); // interface 0
    ret = usb_clear_halt(devHandle, BULK_READ_ENDPOINT);
    if(ret) {
        printf("..Failed to claim interface %d: %d : \'%s\'\n", DEFAULT_INTERFACE, ret, strerror(-ret));
        goto bail;
    }
    
    ret = usb_get_descriptor(devHandle, USB_DT_DEVICE, 0x00, owonDescriptorBuffer, 0x12);
    if(ret < 0) {
        printf("..Failed to get device descriptor %04x '%s'\n", ret, strerror(-ret));
        goto bail;
    }
    return devHandle;

bail:
    usb_reset(devHandle);
    usb_close(devHandle);
    return NULL;
}



int channel_setup(usb_dev_handle *devHandle, int channel, char *waveform, double frequency, double amplidude, double offset) {
   if(devHandle == NULL || channel < 1 || channel > 2) 
      return 0;
    int ret;
    char buf[1024];


    // Shape
    sprintf(buf, "SOURce%d:FUNCtion:SHAPe %s", channel, waveform);
    ret = send_query(devHandle, buf, strlen(buf), NULL, 0);
    if(ret < 0) {
        return 0;
    }

    // Frequency
    sprintf(buf, "SOURce%d:FREQuency:FIXed %lfHz", channel, frequency);
    ret = send_query(devHandle, buf, strlen(buf), NULL, 0);
    if(ret < 0) {
        return 0;
    }

    // Amplitude
    sprintf(buf, "SOURce%d:VOLTage:LEVel:IMMediate:AMPLitude %lfVpp", channel,  amplidude);
    ret = send_query(devHandle, buf, strlen(buf), NULL, 0);
    if(ret < 0) {
        return 0;
    }

    // Offset
    sprintf(buf, "SOURce%d:VOLTage:LEVel:IMMediate:OFFset %lfVpp", channel, offset);
    ret = send_query(devHandle, buf, strlen(buf), NULL, 0);
    if(ret < 0) {
        return 0;
    }

    return 1;
}


int channel_set_state(usb_dev_handle *devHandle, int channel, int state) {
    char buf[100];
    int ret;
    if(devHandle == NULL || channel < 1 || channel > 2) 
        return -1;

    sprintf(buf, "OUTPut%d:STATe %s", channel, (state ? "ON": "OFF"));
    ret = send_query(devHandle, buf, strlen(buf), NULL, 0);
    if(ret < 0) {
        return -1;
    }
    return 0;  
}

void release_device(usb_dev_handle *devHandle) {
    usb_reset(devHandle);
    usb_close(devHandle);
}


int main(int argc, char *argv[]) {
    usb_dev_handle *devHandle;

    if(argc  >1 && strcmp(argv[1],"-v") == 0) {
        verbose = 1;
    }

    usb_init();
    if(!devfindOwon()) {
	printf("..No Owon device %04x:%04x found\n", USB_LOCK_VENDOR, USB_LOCK_PRODUCT);
	return 3;
    }
    for(int i = 0; i < locksFound; i++) {
	char buf[256];
	int ret;
        devHandle = connect_to_device(usb_locks[0]);
        if(devHandle == NULL) {
   	 continue;
        }

        ret = send_query(devHandle, OWON_IDN_QUERY, sizeof(OWON_IDN_QUERY), buf, sizeof(buf));
        if(ret > 0) {
	    if(memcmp(buf, OWON_IDN_WANTED_35, strlen(OWON_IDN_WANTED_35))==0) {
                printf(".. OWON DGE2070 found\n");
		model = 2035;
                break;
	    }
	    if(memcmp(buf, OWON_IDN_WANTED_70, strlen(OWON_IDN_WANTED_70))==0) {
                printf(".. OWON DGE2070 found\n");
		model = 2070;
                break;
	    }
	    
            printf("Unknown device '");
            for(int j = 0; j < ret; j++) {
                putchar(buf[j]);
            }
            printf("'\n");
        }
        release_device(devHandle);
	devHandle = NULL;
    }

    if(devHandle == NULL) {
        printf(".. No DGE20xx device found\n");
        return 3;
    }

        
    printf(".. Turn channels off\n");
    channel_set_state(devHandle, 1, 0);
    channel_set_state(devHandle, 2, 0);
    printf(".. Configuure both channels\n");
    channel_setup(devHandle, 1, "SINE", 500000.0, 1.000, 0.000);
    channel_setup(devHandle, 2, "SQUARE", 500000.0, 1.000, 0.000);
    printf(".. Turn both channels on\n");
    channel_set_state(devHandle, 1, 1);
    channel_set_state(devHandle, 2, 1);

    // Wait
    sleep(4);
    printf(".. Turn both channels off\n");
    channel_set_state(devHandle, 1, 0);
    channel_set_state(devHandle, 2, 0);

    release_device(devHandle);
    return 0;
}
