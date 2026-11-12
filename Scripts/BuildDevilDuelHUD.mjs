import * as Fs from 'node:fs/promises';
import * as Path from 'node:path';
import { fileURLToPath as FileURLToPath } from 'node:url';

const Root = Path.resolve(Path.dirname(FileURLToPath(import.meta.url)), '..');
const Children = [];
const Color = (R, G, B, A = 1) => ({ R, G, B, A });
const Ivory = Color(.87, .81, .66);
const Gold = Color(.50, .43, .31);
const Black = Color(.017, .016, .020, .93);

function Properties(Name, X, Y, Width, Height, Anchor = 0, Align = 0, Z = 1) {
    return { WidgetName: Name, Anchors: { X: Anchor, Y: 0, Z: Anchor, W: 0 }, Offsets: { X, Y, Z: Width, W: Height }, AlignmentX: Align, AlignmentY: 0, RenderOpacity: 1, Visibility: 3, ZOrder: Z, bAutoSize: false };
}

function Rect(Name, X, Y, Width, Height, Tint, Anchor = 0, Align = 0, Z = 1) {
    Children.push({ Class: 'UImage', Properties: { ...Properties(Name, X, Y, Width, Height, Anchor, Align, Z), ColorAndOpacity: Tint, TexturePath: '' } });
}

function Text(Name, Value, X, Y, Width, Height, Size, Tint, Anchor = 0, Align = 0, Justification = 0, Z = 8) {
    Children.push({ Class: 'UTextBlock', Properties: { ...Properties(Name, X, Y, Width, Height, Anchor, Align, Z), Text: Value, FontSize: Size, FontPath: 'Assets/Fonts/Pretendard.json', ColorAndOpacity: Tint, Justification } });
}

for (const [Prefix, Anchor, Align, Sign] of [['Player', 0, 0, 1], ['Rival', 1, 1, -1]]) {
    const X = Value => Value * Sign;
    Rect(Prefix + 'Frame', X(61), 63, 726, 38, Gold, Anchor, Align);
    Rect(Prefix + 'Well', X(62), 64, 724, 36, Black, Anchor, Align, 2);
    Rect(Prefix + 'Trail', X(65), 68, 718, 27, Color(.49, .12, .09), Anchor, Align, 3);
    Rect(Prefix + 'Bar', X(65), 68, 718, 27, Ivory, Anchor, Align, 4);
    Rect(Prefix + 'Danger', X(65), 68, 718, 27, Color(.68, .14, .09), Anchor, Align, 5);
    Rect(Prefix + 'Sheen', X(65), 68, 718, 3, Color(1, .96, .82, .43), Anchor, Align, 6);
    Rect(Prefix + 'Flash', X(65), 68, 718, 27, Color(1, .97, .85), Anchor, Align, 7);
    Rect(Prefix + 'GuardWell', X(65), 106, 210, 3, Color(.03, .032, .038, .85), Anchor, Align, 2);
    Rect(Prefix + 'Guard', X(65), 106, 210, 2, Color(.49, .51, .53, .8), Anchor, Align, 3);
    Text(Prefix + 'Name', 'DEVIL JIN', X(63), 27, 350, 30, 27, Color(.94, .92, .86), Anchor, Align, Anchor ? 2 : 0);
    Text(Prefix + 'Role', Prefix === 'Player' ? 'P1' : 'CPU', X(65), 117, 100, 20, 15, Color(.66, .64, .59), Anchor, Align, Anchor ? 2 : 0);
    for (let Win = 1; Win <= 2; Win++) {
        const Offset = 737 + (Win - 1) * 26;
        Rect(Prefix + 'RoundWell' + Win, X(Offset), 109, 20, 9, Gold, Anchor, Align, 2);
        Rect(Prefix + 'RoundEmpty' + Win, X(Offset + 1), 110, 18, 7, Black, Anchor, Align, 3);
        Rect(Prefix + 'Win' + Win, X(Offset + 2), 111, 16, 5, Ivory, Anchor, Align, 4);
    }
}

Text('TimerShadow', '60', 2, 29, 150, 79, 67, Color(.005, .005, .008, .8), .5, .5, 1, 5);
Text('Timer', '60', 0, 27, 150, 79, 67, Color(.97, .94, .86), .5, .5, 1, 6);
Rect('TimerRule', 0, 111, 49, 1, Gold, .5, .5);
Text('RoundNumber', '1', 0, 119, 60, 18, 14, Color(.66, .64, .59), .5, .5, 1);
Rect('PauseShade', 0, 0, 1920, 1080, Color(0, 0, 0, .22), 0, 0, 15);
Text('BannerShadow', '', 2, 374, 1100, 122, 103, Color(0, 0, 0, .65), .5, .5, 1, 20);
Text('Banner', '', 0, 372, 1100, 122, 103, Color(.97, .94, .85), .5, .5, 1, 21);

const Canvas = { Class: 'UCanvasPanel', Properties: { ...Properties('Root', 0, 0, 0, 0), Anchors: { X: 0, Y: 0, Z: 1, W: 1 } }, Children };
await Fs.writeFile(Path.join(Root, 'Assets/DevilDuel/DuelHUD.widget'), JSON.stringify({ Version: 2, ParentClass: 'UUserWidget', DesignSize: [1920, 1080], Root: Canvas }, null, 4) + '\n');
