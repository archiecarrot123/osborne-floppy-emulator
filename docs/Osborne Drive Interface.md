[Technical manual from p56 ](https://archive.org/details/bitsavers_osborneosbne1TechnicalManual1982_19169707/page/n3/mode/2up?view=theater) 
![[Pasted image 20240528100331.png]]

pins of interest

| Pin | Name           | To (controller or drive) | Comments                                                                                                                       |
| --- | -------------- | ------------------------ | ------------------------------------------------------------------------------------------------------------------------------ |
| 8   | Index          | Con                      | provides a signal every time a new sector is reached (every 200ms) (low when this occurs) ![[Pasted image 20240528125538.png]] |
| 10  | Drive Select 1 | Drv                      | Recieve signals for drive 1 on Direction select, Step, write data, write gate head load<br>If on then recieve (I think)        |
| 12  | Drive select 2 | Drv                      | Recieve signals for drive 2 on Direction select, Step, write data, write gate head load<br>If on then recieve (I think)        |
| 14  | NC             | ???                      | ???                                                                                                                            |
| 16  | 4Mhz clock     | ???                      |                                                                                                                                |
| 18  | Dir            | Drv                      | Direction select: whether the drive head moves towards the center of the disk                                                  |
| 20  | Step           | Drv                      | Related to changing dir                                                                                                        |
| 22  | Write Data     | Drv                      | Data is sent through this to write to the drive                                                                                |
| 24  | Write Gate     | Drv                      | Active to write, inactive to read                                                                                              |
| 26  | Track 00       | Con                      | Active when the drive is **not** on track 0 **OR** tne drive is not selected                                                   |
| 28  | Write Protect  | Con                      | Whether or not a write protected disk is in the drive                                                                          |
| 30  | Read Data      | Con                      | Provides "Raw Data" (clock and data together)                                                                                  |
| 32  | Side Select    | ???                      |                                                                                                                                |
| 34  | Late           | ???                      |                                                                                                                                |
|     |                |                          |                                                                                                                                |

Information from:
[Generic floppy manual](https://web.archive.org/web/20230328214547/http://www.bitsavers.org/pdf/shugart/SA4xx/39019-1_SA400L_OEM_Manual_Nov82.pdf)
