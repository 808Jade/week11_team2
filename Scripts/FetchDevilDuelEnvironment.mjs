import * as Fs from 'node:fs/promises';
import * as Path from 'node:path';
import * as Crypto from 'node:crypto';
import { fileURLToPath as FileURLToPath } from 'node:url';

const Root = Path.resolve(Path.dirname(FileURLToPath(import.meta.url)), '..');
const SourceRoot = Path.join(Root, 'Build', 'DevilDuelSources');
const OutputRoot = Path.join(Root, 'Assets', 'DevilDuel', 'Environment');
const Models = ['modular_urban_apartments_facade', 'rollershutter_door', 'modular_industrial_pipes_01', 'modular_electric_cables', 'exterior_aircon_unit', 'street_lamp_01', 'metal_trash_can', 'trashbag', 'plastic_crate_01'];
const Textures = ['worn_asphalt', 'concrete_floor_worn_001'];

async function GetJSON(URL) {
    const Response = await fetch(URL);
    if (!Response.ok) {
        throw new Error(`${URL}: HTTP ${Response.status}`);
    }
    return Response.json();
}

async function Download(Resource, Destination) {
    const Target = Path.resolve(Destination);
    if (!Target.startsWith(SourceRoot + Path.sep) || !['dl.polyhaven.org', 'raw.githubusercontent.com'].includes(new URL(Resource.url).hostname)) {
        throw new Error('Invalid environment asset destination or host');
    }
    try {
        const Existing = await Fs.readFile(Target);
        if (Crypto.createHash('md5').update(Existing).digest('hex') === Resource.md5) {
            return;
        }
    } catch (Error) {
        if (Error.code !== 'ENOENT') {
            throw Error;
        }
    }
    const Response = await fetch(Resource.url);
    if (!Response.ok) {
        throw new Error(`${Resource.url}: HTTP ${Response.status}`);
    }
    const Bytes = Buffer.from(await Response.arrayBuffer());
    if (Crypto.createHash('md5').update(Bytes).digest('hex') !== Resource.md5) {
        throw new Error('Asset checksum mismatch: ' + Resource.url);
    }
    await Fs.mkdir(Path.dirname(Target), { recursive: true });
    await Fs.writeFile(Target, Bytes);
}

const Manifest = [];
const Catalog = await GetJSON('https://api.polyhaven.com/assets?type=models');
await Fs.mkdir(OutputRoot, { recursive: true });

for (const Id of [...Models, ...Textures]) {
    const Files = await GetJSON('https://api.polyhaven.com/files/' + Id);
    const Directory = Path.join(SourceRoot, Id);
    if (Models.includes(Id)) {
        const Model = Files.gltf['1k'].gltf;
        const Jobs = [[Model, Path.join(Directory, Id + '.gltf')], ...Object.entries(Model.include).map(([Name, Resource]) => [Resource, Path.join(Directory, Name)])];
        for (let Index = 0; Index < Jobs.length; Index += 4) {
            await Promise.all(Jobs.slice(Index, Index + 4).map(([Resource, Destination]) => Download(Resource, Destination)));
        }
        Manifest.push({ Id, Kind: 'Model', Source: 'https://polyhaven.com/a/' + Id, License: 'CC0-1.0', LicenseURL: 'https://polyhaven.com/license', Authors: Catalog[Id]?.authors ?? {}, Input: Path.relative(Root, Path.join(Directory, Id + '.gltf')).replaceAll('\\', '/'), Files: Jobs.map(([Resource, Destination]) => ({ URL: Resource.url, MD5: Resource.md5 })) });
    } else {
        const Channels = { Diffuse: Files.Diffuse ?? Files.diff, Normal: Files.nor_gl };
        const Outputs = {};
        for (const [Name, Channel] of Object.entries(Channels)) {
            const Resource = Channel['2k'].jpg;
            const Destination = Path.join(Directory, Id + '_' + Name + '.jpg');
            await Download(Resource, Destination);
            Outputs[Name] = Path.relative(Root, Destination).replaceAll('\\', '/');
        }
        Manifest.push({ Id, Kind: 'Texture', Source: 'https://polyhaven.com/a/' + Id, License: 'CC0-1.0', LicenseURL: 'https://polyhaven.com/license', Input: Outputs });
    }
    process.stdout.write('Downloaded ' + Id + '\n');
}

const SkyId = 'modern_buildings_night';
const SkyFiles = await GetJSON('https://api.polyhaven.com/files/' + SkyId);
const SkyDestination = Path.join(SourceRoot, SkyId, SkyId + '.jpg');
await Download(SkyFiles.tonemapped, SkyDestination);
Manifest.push({ Id: SkyId, Kind: 'Sky', Source: 'https://polyhaven.com/a/' + SkyId, License: 'CC0-1.0', LicenseURL: 'https://polyhaven.com/license', Input: Path.relative(Root, SkyDestination).replaceAll('\\', '/') });
const FontRevision = '7aeb0698819be2b4097dae8ec8fe6a795e5cf3ae';
const FontBase = 'https://raw.githubusercontent.com/orioncactus/pretendard/' + FontRevision + '/';
const FontFiles = [{ url: FontBase + 'packages/pretendard/dist/public/static/Pretendard-Bold.otf', md5: 'f8a9b84216af5155ffe0e8661203f36f', Name: 'Pretendard-Bold.otf' }, { url: FontBase + 'LICENSE', md5: 'eb9bdd8a4abced4876ee0e2a99a62938', Name: 'OFL.txt' }];
for (const File of FontFiles) {
    await Download(File, Path.join(SourceRoot, 'Pretendard', File.Name));
}
Manifest.push({ Id: 'Pretendard', Kind: 'Font', Source: 'https://github.com/orioncactus/pretendard', License: 'OFL-1.1', LicenseURL: FontBase + 'LICENSE', Authors: { 'Kil Hyung-jin': 'Font' }, Input: 'Build/DevilDuelSources/Pretendard/Pretendard-Bold.otf', Files: FontFiles.map(File => ({ URL: File.url, MD5: File.md5 })) });
await Fs.writeFile(Path.join(OutputRoot, 'Sources.json'), JSON.stringify(Manifest, null, 4) + '\n');
