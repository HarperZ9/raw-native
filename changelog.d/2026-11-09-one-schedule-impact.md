- **One schedule for a blow: the swing decides the hit, and every channel lands on its tick**
  (`raw/game/action/contact.hpp`, `schedule.hpp`, `raw/renderer/impact.hpp`, `raw_native_cli action-impact`,
  `docs/action-schedule.md`). Contact is a capsule on the swing, found on the shared bit-exact math in the
  expression order of Low Water's JS: 40 of 40 contact traces, where the JS world found every contact itself,
  replay byte for byte, and the kernel agrees with the G3 capsule on 99.998% of 1.65 million targets, the rest
  within 1e-9 m of the boundary. Pose, hit volume, trail, smear, contact, hit-stop, shake, rumble, sound, burst
  and hitmarker are cues on the 60 Hz sim tick: 0 same-tick exceptions over 186 contacts, and a sound one tick
  late is caught on all of them. The trail shows only on active frames (24,000 dumped frames), hit-stop is 0
  light and 2 heavy, the camera ports Low Water's fightcam to within 0.000001 px (6 px peak at 1080p, 3 px
  under a tell), and each contact's sound starts within 0.05 ms of the frame that shows its burst. Missed:
  full-length runs at 640 x 360 count one flash a second in a window (under the limit of three), and the
  spring layer of the shake never reaches 1 px, so its size goes unchecked.
