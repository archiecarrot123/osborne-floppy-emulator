
Drive controller chip: 1793 LSI chip

[Osborne Technical manual ](https://archive.org/details/bitsavers_osborneosbne1TechnicalManual1982_19169707/page/n3/mode/2up?view=theater) Pages of interest: 110, 57-60 124 (internet archive page number)
[Drive controller spec sheet](https://ia902902.us.archive.org/24/items/rearc_fd1771-01-floppy-disk-formatter-controller-manual-1980-06/FD1771-01%20Floppy%20Disk%20Formatter-Controller%20Manual%20(1980-06)(Western%20Digital)(US).pdf)
[Generic floppy manual](https://web.archive.org/web/20230328214547/http://www.bitsavers.org/pdf/shugart/SA4xx/39019-1_SA400L_OEM_Manual_Nov82.pdf)
#### pins of interest
![[Pasted image 20240528100331.png]]



| Pin | Name           | To (controller or drive) | Comments (descriptions of low/high on drive/connecctor side, which is what we will hook into)                                                                    |
| --- | -------------- | ------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 4   | TG43           | Drv                      | Low if track number greater than 43                                                                                                                              |
| 6   | Early          | Drv                      | Low if write data should be shifted earlier                                                                                                                      |
| 8   | Index          | Con                      | provides a signal every time a index is reached (every 200ms) (low when this occurs) ![[Pasted image 20240528125538.png]]                                        |
| 10  | Drive Select 1 | Drv                      | Recieve signals for drive 1 on Direction select, Step, write data, write gate head load<br>Low if selected                                                       |
| 12  | Drive select 2 | Drv                      | Recieve signals for drive 2 on Direction select, Step, write data, write gate head load<br>Low if selected                                                       |
| 14  | NC             | N/A                      | Not Connected                                                                                                                                                    |
| 16  | 4Mhz clock     | Drv                      | Inverted CPU clock                                                                                                                                               |
| 18  | Direction      | Drv                      | Direction select: whether the drive head moves towards the center of the disk when step is triggered                                                             |
| 20  | Step           | Drv                      | Low to move head                                                                                                                                                 |
| 22  | Write Data     | Drv                      | Data is sent through this to write to the drive                                                                                                                  |
| 24  | Write Gate     | Drv                      | Low when data is being written                                                                                                                                   |
| 26  | Track 00       | Con                      | High when the track is not selected (stores metadata about the file)                                                                                             |
| 28  | Write Protect  | Con                      | Whether or not a write protected disk is in the drive                                                                                                            |
| 30  | Read Data      | Con                      | Provides "Raw Data" (clock and data together)                                                                                                                    |
| 32  | Side Select    | Drv?                     | which side should be used for double sided floppies?? <br>or<br>Inversion of what the schematic calls SSO or what the datasheet calls RG. Low if sync byte found |
| 34  | Late           | Drv?                     | Low if write data should be shifted later                                                                                                                        |


###### Descriptions from:
Archie:
>All inputs are buffered and pulled up to 5V on the mainboard  
  All outputs are inverted on the mainboard  
  The inverters are open-collector so we'll need  
  pull-up resistors if we want to work without the FDDs
  TG43: Low if track number greater than 43 (from FDC???)  
  Early: Low if write data should be shifted earlier  
  Index: Pull this low when the index hole goes past  
  DS 1: Low if drive 1 selected  
  DS 2: Low if drive 2 selected  
  4MHz: This is the (inverted) CPU clock from the mainboard  
  Direction: High to move head out, low to move in   
  Step: Pulse signal for moving the head (low=move)  
  Write Data: The data from the FDC to the FDD  
  Write Gate: Low when the FDC is writing data  
  Track 00: High if track no. 00 is detected during seek operation  
  Write Protect: "Signal for inhibiting write operation"  
  Read Data: This goes into a circuit that is probably a VFO but I can't be bothered  
  Side Select/Read Gate: Inversion of what the schematic calls SSO or what the datasheet calls RG. Low if sync byte found  
  Late: Low if write data should be shifted later

[Generic floppy manual](https://web.archive.org/web/20230328214547/http://www.bitsavers.org/pdf/shugart/SA4xx/39019-1_SA400L_OEM_Manual_Nov82.pdf)


All inputs are buffered and pulled up to 5V on the mainboard  
All outputs are inverted on the mainboard  
The inverters are open-collector so we'll need  
pull-up resistors if we want to work without the FDDs

TG43: Low if track number greater than 43 (from FDC???)  
Early: Low if write data should be shifted earlier  
Side Select/Read Gate: 


#### Specs
        track bit density, innermost track (track 1)
                2500-2900 bpi for single density,
                5600-5900 bpi for double density
        data frequency
                125Kb/sec single density FM 
                250Kb/sec double density MFM
                
        rotation speed 300 RPM
        track density 48 tpi (tracks per inch)
	  typical head stepper motor angle per step: 3.6 degrees
        capacity 250KB single density (less formatted)
                500KB single density (less formatted)
        drive track radius from center (inner to outer) 
                1.542 inches to 2.250 inches  (maybe 35 track?)
		1.354 inches to 2.25 inches
		see [ 35-track drives and media](https://archive.is/jfxds#35)
		see [100 tpi 5.25" drives](https://archive.is/jfxds#100tpi)
        Track width: .012-.013 inch
	Track erase width: about .006 inches either side of width
		also see [erase notes](https://archive.is/jfxds#erase)
        Track interval .0208 inches
        resistor terminator: resistor pack, 
		Generally 100-150 ohms, may be 220 to +5, 330 to gnd - check docs 
	
        Diskette and Media: 
                Mylar, 0.003 in thick (.0008mm)
                iron oxide coating, 300 oersteds coercivity
                same media for single and double density media
                slot for read/write head: 
                   1.210" long for 35 track, 1.370" for 40 track
                   slot is .13" from outer edge of envelope for BOTH 
                   see notes on "35 track drives and media"
        orientation of index hole in diskette sleve/cover:
                (hold disk face up, slot down toward you)
                right side of spindle hole, slightly toward slot
                hard sectored: 
                      inner disk has sector holes to mark start of each sector
                      index hole on inner disk is centered between two sector holes

#### Sectoring:
>When a soft-sectored disk is low-level "formatted", each track is written with a number of bytes calculated to fit within 360 degrees at the highest expected motor speed. Special bit patterns are written right before the location where a sector should start, and serve as identifiers, similar to the punched holes used by hard-sectored disks. Thus, the full constellation of punched holes is not needed, and only a single hole is retained, to indicate the start of the track (3+1⁄2-inch disks use an alignment pin rather than a hole). If the motor is spinning any slower than the highest acceptable speed, which is usually the case, the data will fit in fewer than 360 degrees, resulting in a gap at the end of the track. Additionally, if a sector were to be rewritten on a drive running faster than the drive was running when the track was formatted, the new data would be larger (occupy more degrees of rotation) than the original sector. Therefore, during formatting a gap must be left between sectors to allow a rewritten sector to be larger without over-writing the following sector.
[- wikipedia](https://en.wikipedia.org/wiki/Floppy_disk_format) (no further source provided)

Osborne 1 uses 10 sectors per track 256 bytes each, but these are reoported as 20 sesctors 128 bytes each to CP/M

#### Encoding
The osborne Uses [FM encoding](https://en.wikipedia.org/wiki/Frequency_modulation_encoding)  

First a signal is issued to show that a bit will be now sent, and then at the next pulse one is sent if it's a 1 and none i f it's a 0 so **01000001**, will become 1**0**1**1**1**0**1**0**1**0**1**0**1**0**1**1**
![[Pasted image 20240531105600.png]]
as each index occurs every 200 ms, and there are 10 sectors per disk, I think we have 20ms per sector
#### Sequence of signals
###### Startup
as the disk is started the Write gate will be held "Inactive" (high I think)
It is then powered on and stepped out until track 00 is given (we could just straightaway give this on our emulator)
Drive select is given (check if this is the right one)

##### Changing traccks
drive select needs to be Low
direction is set
write gate needs to be "incactive" (High I think)
step signal is pulsed to low once for each change

Stepping sampe
![[Pasted image 20240531100456.png]]
##### 