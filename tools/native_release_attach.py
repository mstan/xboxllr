"""Attach verified corresponding source to a pristine native package."""
from pathlib import Path
import json
import shutil
import zipfile
from native_release_runtime import descriptor, sha


def verify(path, *, dependency=False, revision=None):
    name = 'DEPENDENCY-SOURCES.json' if dependency else 'SOURCE-RELEASE.json'
    schema = 'xboxllr.native-dependency-source.v1' if dependency else 'xboxllr.source-release.v1'
    with zipfile.ZipFile(path) as archive:
        record = json.loads(archive.read(name))
        if (record.get('schema') != schema or archive.testzip() is not None
                or len(archive.namelist()) != len(set(archive.namelist()))
                or set(archive.namelist()) != set(record['files']) | {name}):
            raise ValueError('Invalid corresponding-source archive')
        import hashlib
        for file, expected in record['files'].items():
            if hashlib.sha256(archive.read(file)).hexdigest() != expected:
                raise ValueError('Corresponding-source hash differs: ' + file)
        if revision and revision not in record['revisions'].values():
            raise ValueError('Source does not contain the selected framework revision')
    return record


def attach(stage, sources, dependency, revision):
    stage = Path(stage)
    metadata = descriptor(stage)
    sources = [Path(path) for path in sources]
    dependency = Path(dependency)
    manifests = [verify(path, revision=revision) for path in sources]
    dependency_manifest = verify(dependency, dependency=True)
    artifacts = [*sources, dependency]
    if len({p.name.casefold() for p in artifacts}) != len(artifacts):
        raise ValueError('Corresponding-source filenames collide')
    target = stage / 'source'
    target.mkdir(exist_ok=True)
    records = []
    for path in artifacts:
        destination = target / path.name
        if destination.exists():
            raise ValueError('Existing source attachment is preserved')
        shutil.copy2(path, destination)
        relative = destination.relative_to(stage).as_posix()
        digest = sha(path)
        if sha(destination) != digest:
            raise ValueError('Copied source differs')
        metadata['files'][relative] = digest
        records.append(dict(file=relative, sha256=digest, bytes=path.stat().st_size))
    availability = target / 'SOURCE-AVAILABILITY.json'
    availability.write_text(json.dumps(dict(schema='xboxllr.accessible-source.v1',
        framework_revision=revision, artifacts=records,
        source_revisions=[m['revisions'] for m in manifests],
        dependency_revisions={k: v for k, v in dependency_manifest.items() if k != 'files'}), indent=2)+'\n')
    metadata['files'][availability.relative_to(stage).as_posix()] = sha(availability)
    (stage/'release.json').write_text(json.dumps(metadata, indent=2)+'\n')
