# GLOAM

GLOAM is a groovebox firmware for the M-VAVE FM-1, for witch house and melodic dubstep. Five lanes (drums with a
looseness every hit keeps, a bass that is an 808, a reese or a beat-locked wobble, minor chords that duck under the
kick, a choir that sings a vocal bank with pitch and formant apart, and rain, crackle and wind), SCREW to slow and
lower everything together, a reverb that freezes, lantern pads, and an arranger of up to 32 sections with follow
actions, transitions and macro ramps.

![GLOAM's drum lane in a drop: eight sounds, each hit drawn where it plays](manual/img/screen-drums.png "GLOAM on the FM-1's screen")

Version {{VERSION}}. **Untested on a real FM-1 so far:** it builds, passes its tests in fm1-base's simulator, and
runs in the browser emulator. Installing is at your own risk; the installer can put M-VAVE's own firmware back.

- [Try it in the browser](emu/): the same code the FM-1 runs, with sound. No FM-1 needed.
- [Install](install/): from Chrome or Edge, with the FM-1 connected by USB. The installer writes the firmware, then
  the choir's vocal bank.
- [Manual](manual/): the lanes, the arranger, the controls.
- [Backup](backup/): keep the settings on your computer.
- [Download {{PKG}}](firmware/{{PKG}}): for the command-line installer, `python3 base/tools/fm1_install.py {{PKG}}`
  (with the bank file beside it).
- [Source](https://github.com/davidpryan/gloam): MPL-2.0. Built on fm1-base and felucca-bsp, from Felucca by Leo
  Kuroshita, Hügelton Instruments.

M-VAVE and FM-1 are trademarks of their owners. GLOAM is not affiliated with M-VAVE.
