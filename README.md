# Fox-Hunt-AM-transmitter-controller-ESP32-Dev-kit-V1
This is an entire C++ code executable code that could be flashed on an ESP32 Dev Kit V1, it modulates the output stage of an AM transmitter with PWM, uses the same pin for Morse code, features a WebGUI to enable you to edit your playlist in the field with a choice of sound files, morse messages, and silence set by seconds
The format for the sound files needs to be encoded in 4 Bit IMA ADPCM at a sample rate of 5KHz, this can be done in Adobe Audition, make sure you process the audio first before converting, first normalise, then apply high cut at 350Hz, and a low cut at 2500Hz, compress twice or more then apply limiter or until the sound is loud and consistent, but avoid overdoing it that would boost up the background noise, once done, apply a 2500Hz Low pass filter to filter out any products before saving it as a 5KHz IMA ADPCM file.
there is no limit in how many files you upload or the number of morse messages, the limit is the 1.4MB partition on the flash of the ESP32, a 22 second sound file is about 49 Kilobytes if I remember correctly, so you can fit enough to compile your own short broadcast segment with sound effects if you want.
PWM and morse come out of Pin 18, which will gate drive a Mosfet that can modulate a transmitter, you will need a logic level gate drive compatible Mosfet such as an IRZ44N through a 47 Ohm resistor with a 100K gate drain resistor, or you need a gate driver IC to drive the typical Mosfets that require 5 volts, just make sure the Mosfet can switch 80KHz
Make sure you have a low pass filter on the output stage, you will need to provide an LC network to both shunt the RF of the transmitter to ground and filter out the PWM, beware as I have not reached this far in the development to come up with component values, I shall warn that going forward on your own may risk causing interference unless you know what you are doing.
Again, has not been tested in the field yet, has been tested with a loundspeaker, make sure you have a means of spectral measurement to check your mask, the Amateur radio bands specify a maximum channel occupation space of 5KHz, lending to a frequency response of 2.5KHz, one of the reason why I chose an odd sample rate besides being a frugal on memory codec. the use of ADPCM has interpolation applied otherwise it generates a lot of harmonics that could spill outside the channel occupation, the harmonics that give you that chirpy sound found in childrens toys and 1980's game consoles, since its an early form of compression with minimal processor overhead, if the ADPCM is found to be too troublesome, despite later adding Low pass filters, there is consideration of adding an SD memory controller to the ESP32 and use 8 Bit PCM instead, 

but first, will explore all avenues first for further refinement in the Audio engine to see if I can keep this setup at the lowest component count as possible, ie: controller -> Modulator Mosfet -> Transmitter Final.

There will be a later version coming a little later that will be suitabke for FM AND SSB transceivers as well, this will involve modifications to the Aidio engine to drive the ESP32 DAC, and replace the morse pulse with a morse tone.


##########################################################################################

PREGRESS REPORT

##########################################################################################

15/04/2026 UPDATE: Some further work has been done to improve the playback quality, also there has beeh support added for SD card support to allow more and larger sound files to be uploaded, this goes in line withs with added support for playback of unsigned 8 bit WAV files, since True PCM will provice cleaner audio, and since there is no apps available for iPhone that can pruduce 4 Bit ADPCM, you can now with this later version use a smartphone to do all your playlist upload and configurations in the field, you will no longer need a laptop, however without an SD card, you only have 1.4MB of storage onboard the ESP32 so that can fill up very quickly, recommended to get an SD card for more capacity, make sure its no larger than 32GB!

This code has not been tested yet, I will later provide a review when I test it.

Cheers and have fun with it.

Cheers, Christopher O'Reilly VK4MTV
