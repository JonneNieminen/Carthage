# Carthage theme

The look of Carthage, in one place. The values live in [`tokens.css`](tokens.css); this file says what
they are for. If the two disagree, `tokens.css` is right and this file needs fixing.

## The idea

Carthage's harbour, the *cothon*, was a ring of water around an island with one channel to the sea.
Seen from above it is a **C** around a dot. That is the mark, and three things follow from it:

- **One centre.** The session is the island; panels sit around it and never bury it.
- **One channel.** Tracks leave the harbour as horizontal lanes: the timeline.
- **One light.** A single blue marks whatever is live. Everything else is slate.

The palette is stone, water and sky: quiet greys that stay out of the way during long sessions, and
one accent the eye can always find.

## Colour

### Harbour palette

Fixed colours, the same in both themes. The app icon uses graphite, mist and sky.

| Name | Hex | Token |
| --- | --- | --- |
| Graphite | `#1C2127` | `--graphite` |
| Steel | `#3A434D` | `--steel` |
| Grey | `#8B96A1` | `--grey` |
| Mist | `#D5DCE3` | `--mist` |
| Ice | `#EEF2F5` | `--ice` |
| Sky | `#8FD0F5` | `--sky` |
| Sky, deep | `#4FA8DC` | `--sky-deep` |

### Roles

Components use these, never the palette names directly. The app ships **dark only in v1**; the light column is used by
the README artwork and is kept ready for a later light theme.

| Token | Dark | Light | Use |
| --- | --- | --- | --- |
| `--well` | `#14181C` | `#DDE3E9` | Recessed: timeline and piano-roll canvas, meter wells |
| `--bg` | `#1C2127` | `#EEF2F5` | Window background (graphite) |
| `--bg2` | `#232A31` | `#E4E9EE` | Panels: browser, mixer, inspector, track headers |
| `--card` | `#2B333B` | `#FFFFFF` | Raised things: popovers, fields, neutral clips |
| `--hover` | `#353E47` | `#F6F8FA` | A raised surface under the pointer |
| `--line` | white 8% | graphite 10% | Hairlines between surfaces, beat lines |
| `--line-strong` | white 16% | graphite 20% | Bar lines, field borders |
| `--text` | `#EEF2F5` | `#1C2127` | Primary text |
| `--muted` | `#9AA5B0` | `#56616C` | Secondary text and labels |
| `--faint` | `#5D6873` | `#99A3AD` | Disabled, placeholders. Not for text that must be read |
| `--accent` | `#8FD0F5` | `#4FA8DC` | Fills: playhead, selection, focus ring, active tool |
| `--accent-text` | `#8FD0F5` | `#1F6FA8` | The accent as text or a thin line |
| `--on-accent` | `#10242F` | `#FFFFFF` | Text on an accent fill |

`--text` and `--muted` meet WCAG AA on `--bg`, `--bg2` and `--card` in both themes. In light, use
`--accent-text` for accent-coloured text; `--accent` is too pale to read there.

### Lanes

Track colours. A track takes one; its clips, waveform, meter and mixer strip follow. New tracks cycle
through them in this order. Light values are deeper so clips keep their weight on a pale canvas.

| Lane | Dark | Light |
| --- | --- | --- |
| Sky | `#8FD0F5` | `#3F97CC` |
| Tide | `#7FCFC2` | `#2F9C8D` |
| Kelp | `#A8CF8C` | `#6B9A48` |
| Sand | `#E0C27E` | `#B08A2C` |
| Coral | `#EC9C8B` | `#CF6A55` |
| Murex | `#B9A0EA` | `#7F63C9` |
| Rose | `#E597B8` | `#C65E8A` |
| Slate | `#8B96A1` | `#66717C` |

Murex is the sea snail behind Tyrian purple, the dye Carthage's founders traded.

### Signal

`--ok`, `--warn` and `--clip` (green, amber, red) show signal state and nothing else: meter zones,
record arm, clipping, warnings, destructive actions.

## Type

| Voice | Token | Use |
| --- | --- | --- |
| Interface | `--font-ui` | Everything in the app. Barlow, embedded in the app, 13 px by default |
| Dense | `--font-dense` | Lane headers, toolbar labels, dense rows. Barlow Semi Condensed |
| Time | `--font-time` | Timecode, bars.beats.ticks, dB, Hz, sample counts: figures that must not jitter as they change. IBM Plex Mono |
| Display | `--font-display` | The wordmark and repo artwork only. Futura Medium |

Sizes: 11, 12, **13**, 15, 20, 28 px (`--text-xs` … `--text-2xl`). Weights: 400 for text, 600
(semibold) for labels and titles; the app embeds only these two (DECISIONS W45). The typefaces are
under the SIL Open Font License; its texts ship with the app. Sentence case everywhere; no all-caps labels.

The wordmark is lowercase `carthage` in Futura Medium, tracked at −0.02 em. Futura's round `c`, `a`,
`g` and `e` repeat the ring.

## Shape, space, motion

- **Radius** follows nesting: 3 px clips and meter segments, 6 px buttons and fields, 10 px cards and
  popovers, 14 px dialogs. Clips stay nearly square so their edges read as exact positions in time.
- **Lines** are 1 px. Separate surfaces with a shift in tone first and a hairline second; no shadows on
  controls. Only things that float (popovers, dialogs, the app icon) cast a shadow.
- **Spacing** is a 4 px grid: 4, 8, 12, 16, 24, 32.
- **Motion** answers an action and is short: 120 ms for state changes, 220 ms for panels, both on
  `--ease`. Nothing moves on its own during playback except the playhead and the meters. Respect
  the system's reduced-motion setting.

## The mark

"Cothon", drawn on a 64 grid:

- ring: circle of radius 17 centred at (32, 32), stroke 5, round caps, with an 84° opening to the east;
- island: filled circle of radius 6 at the centre.

| File | For |
| --- | --- |
| `icon/carthage-icon.svg` | App icon master: 1024 canvas, 824 tile, radius 185 |
| `logo/carthage-symbol-on-dark.svg` | The mark on dark surfaces (mist ring, sky dot) |
| `logo/carthage-symbol-on-light.svg` | The mark on light surfaces (steel ring, deep sky dot) |
| `logo/carthage-symbol-mono.svg` | One colour, follows `currentColor`: toolbars, menu bar, print |
| `logo/favicon.svg` | Dark tile at 64 px |

Keep clear space of one dot diameter around the mark. Don't rotate it, close the ring, or recolour it
beyond these variants. The app icon is the dark tile in both themes.

## In the app

The app is C++ (JUCE), so it cannot read `tokens.css`. It keeps one theme header that mirrors the dark
column of this guide value for value, with the same names (`--bg2` → `theme::bg2`). Change a value
here first, then there. Components take colours, sizes and radii from that header and never from
literals. Futura is not shipped with the app: the wordmark appears in-app only as the outlined artwork.

## Artwork

`banners/art.html` draws the banner, the social preview and the cards from these tokens;
`banners/render.sh` renders them into `docs/art`. The banner's harbour is the mark opened up: ship
sheds around the inside of the ring, and track lanes with clips leaving through the channel.
