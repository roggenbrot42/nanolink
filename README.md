# Nanonlink

Nanolink is a reliable, packet oriented, connection based data link layer protocol. The main purpose of the
protocol is to reliably transfer data packets of variable length to another node.

This repositoriy contains the protocol reference and data-link layer implementation without the physical layer 
implementation (Modulation and FEC) and interface.

## About this Code

This code was written as part of the MOVE-II project at the Technical University of Munich. It's run on the UHF/VHF and S-Band radio transceivers 
of the satellite. The target architecture was a C3000 softcore-microprocessor on an FPGA.
Hence, the many defines checking for the C3000. I more or less copied the code directly from the
transceiver's firmware, so there are missing dependencies, which I will not disclose in this repository.

## Using this Code
The Nanolink code was written so that all control is excerted by an upper layer control function. Frames flow from that controlling instance to the Nanolink code and back. The main structure ```struct nanolink_state``` holds all the state variables of the protocol. The main structure must be initialized before all else. It is also referenced by virtually all Nanolink functions, so if you're more into C++, writing a wrapper or porting this code should be a piece of cake. 

As you might have noticed, this code avoids the use of dynamic memory allocation, which was a design decision to avoid memory overruns. The code ran on a machine with a total of 16K of memory, so knowing how much memory the code needed was imperative. 

### Reception

To receive Nanolink frames, simply copy them into a buffer in your upper control code and call ```nanolink_receive()```. The member ```sp``` in ```struct nanolink_state``` is meant as a buffer to hold exactly one frame.

Only the first three parameters are mandatory, you can actually call ```nanolink_receive(nanolink_struct, recv_buffer, 0,0)``` as well to receive without relaying the data to other protocol layers. That's useful when your buffers overrun and you still want to keep receiving.

### Transmission

#### Unpacking from an octet buffer
To make a proper ```struct nanolink_frame``` from an octet buffer, you can use ```nanolink_unpack()```. In our implementation, we delivered entire Nanolink frames to the transceiver, but didn't fill out all of the header. This allowed us to have the user make decisions about the virtual channel and so on without needing an extra interface.

#### Using the scheduler
Now that you have a proper Nanolink frame, you can enqueue it using ```nanolink_enqueue()```. Depending on your implementation, you can either use a fixed pool of Nanolink frames or just discard them after use. I found it more practical to keep track of all frames and recycle them, as that makes finding bugs easier.

After enqueueing the frame, it is subject to the deficit round robin scheduler. This scheduler is fair and guarantees a minimum data rate to every channel. The DRR was chosen because we wanted to make sure rogue programs couldn't stop telemetry from passing through the link. It also implements the other traffic classes we needed in the MOVE-II project.

To get the next frame from the scheduler, simply call ```nanolink_get_next()``` and process it using ```nanolink_send()```. Afterwards, call your platform specific function to transmit the data which was moved to the location of the ```octet* dest```.

### Protocol states
Protocol states are handled by the function ```nanolink_cstate_handler()```. All state changes, except for timeouts are handled internally by Nanolink when ```nanonlink_send() ``` or ```nanolink_receive()``` are called.

## Third Party Code

This code uses the linux kernel's doubly linked list to keep track many data structures. The code was adapted  for userland use. I don't remember where I got the code from, but thanks to that person for putting in the effort.

## License
See [the license file](LICENSE). If you want to use this code under a different license that is not covered by the current one, contact me.

## Further reading
[NANOLINK: A ROBUST AND EFFICIENT PROTOCOL FOR SMALL SATELLITE
RADIO LINKS](https://www.researchgate.net/publication/303922851), N. Appel, S. Rückerl, M. Langer, The 4S Symposium 2016

[Software-Defined Communication on the Nanosatellite MOVE-II](
https://www.researchgate.net/profile/Martin_Langer4/publication/321798220_MOVE-II_THE_MUNICH_ORBITAL_VERIFICATION_EXPERIMENT_II/links/5a326d3b458515afb6db3bc8/MOVE-II-THE-MUNICH-ORBITAL-VERIFICATION-EXPERIMENT-II.pdf), S. Rückerl, N.Appel, M. Langer, International Astonautic Congress, 2018