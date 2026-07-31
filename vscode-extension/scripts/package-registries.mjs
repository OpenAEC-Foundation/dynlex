import { execFile as execFileCallback, spawn } from 'node:child_process';
import { promisify } from 'node:util';
import {
    copyFile,
    cp,
    mkdir,
    mkdtemp,
    readFile,
    rm,
    writeFile,
} from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath, pathToFileURL } from 'node:url';

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const extensionDirectory = path.resolve(scriptDirectory, '..');
const repositoryDirectory = path.resolve(extensionDirectory, '..');
const defaultOutputDirectory = path.join(
    repositoryDirectory,
    'build',
    'vscode-extension',
);
const require = createRequire(import.meta.url);
const { readVSIXPackage, readZip } = require('@vscode/vsce/out/zip');
const vsceCli = require.resolve('@vscode/vsce/vsce');
const reproduciblePackageEpoch = '946684800';
const execFile = promisify(execFileCallback);

export const registryPackages = [
    {
        registry: 'vscode-marketplace',
        publisher: 'impertio',
    },
    {
        registry: 'open-vsx',
        publisher: 'open-aec',
    },
];

export function selectRegistryPackages(registry) {
    if (registry === undefined) {
        return registryPackages;
    }
    const registryPackage = registryPackages.find(
        (candidate) => candidate.registry === registry,
    );
    if (registryPackage === undefined) {
        throw new Error(`Unknown extension registry '${registry}'.`);
    }
    return [registryPackage];
}

export function buildRegistryManifest(manifest, registryPackage) {
    if (manifest.publisher !== 'impertio') {
        throw new Error(
            `Expected canonical publisher 'impertio', found '${manifest.publisher}'.`,
        );
    }
    if (!registryPackages.includes(registryPackage)) {
        throw new Error('Unknown extension registry package.');
    }
    return {
        ...manifest,
        publisher: registryPackage.publisher,
    };
}

export function artifactFileName(manifest, registryPackage) {
    return `${manifest.name}-${manifest.version}-${registryPackage.registry}.vsix`;
}

export function parseProductionDependencyDirectories(
    npmOutput,
    sourceExtensionDirectory,
) {
    const nodeModulesDirectory = path.join(
        sourceExtensionDirectory,
        'node_modules',
    );
    const directories = [];

    for (const outputLine of npmOutput.split(/\r?\n/)) {
        if (outputLine.length === 0) {
            continue;
        }
        const dependencyDirectory = path.resolve(outputLine);
        if (dependencyDirectory === sourceExtensionDirectory) {
            continue;
        }
        const relativePath = path.relative(
            nodeModulesDirectory,
            dependencyDirectory,
        );
        if (
            relativePath.length === 0
            || relativePath.startsWith(`..${path.sep}`)
            || path.isAbsolute(relativePath)
        ) {
            throw new Error(
                `npm reported dependency '${dependencyDirectory}' outside `
                + "the extension's node_modules directory.",
            );
        }
        directories.push(dependencyDirectory);
    }

    return directories;
}

function parseArguments(arguments_) {
    let outputDirectory = defaultOutputDirectory;
    let registry;
    const seenOptions = new Set();

    for (let index = 0; index < arguments_.length; index += 2) {
        const option = arguments_[index];
        const value = arguments_[index + 1];
        if (value === undefined) {
            throw new Error(`Missing value for '${option}'.`);
        }
        if (seenOptions.has(option)) {
            throw new Error(`Option '${option}' was provided more than once.`);
        }
        seenOptions.add(option);
        if (option === '--out-dir') {
            outputDirectory = path.resolve(value);
        } else if (option === '--registry') {
            registry = value;
        } else {
            throw new Error(`Unknown option '${option}'.`);
        }
    }

    return {
        outputDirectory,
        packages: selectRegistryPackages(registry),
    };
}

function shouldCopySource(sourcePath) {
    const relativePath = path.relative(extensionDirectory, sourcePath);
    if (relativePath.length === 0) {
        return true;
    }

    const [topLevelEntry] = relativePath.split(path.sep);
    if (
        topLevelEntry === 'node_modules'
        || topLevelEntry === 'out'
        || topLevelEntry === '.vscode'
        || topLevelEntry === '.vscode-test'
    ) {
        return false;
    }
    return !relativePath.endsWith('.vsix');
}

async function copyProductionDependencies(stagedExtensionDirectory) {
    const npmExecutable = process.platform === 'win32' ? 'npm.cmd' : 'npm';
    const { stdout } = await execFile(
        npmExecutable,
        ['ls', '--omit=dev', '--all', '--parseable'],
        {
            cwd: extensionDirectory,
            encoding: 'utf8',
        },
    );
    const dependencyDirectories = parseProductionDependencyDirectories(
        stdout,
        extensionDirectory,
    );

    for (const dependencyDirectory of dependencyDirectories) {
        const relativePath = path.relative(
            extensionDirectory,
            dependencyDirectory,
        );
        await cp(
            dependencyDirectory,
            path.join(stagedExtensionDirectory, relativePath),
            {
                dereference: true,
                recursive: true,
            },
        );
    }
}

async function runVscePackage(stagedExtensionDirectory, outputPath) {
    await new Promise((resolve, reject) => {
        const child = spawn(
            process.execPath,
            [
                vsceCli,
                'package',
                '--no-dependencies',
                '--out',
                outputPath,
            ],
            {
                cwd: stagedExtensionDirectory,
                env: {
                    ...process.env,
                    PATH: [
                        path.join(extensionDirectory, 'node_modules', '.bin'),
                        process.env.PATH,
                    ].filter(Boolean).join(path.delimiter),
                    SOURCE_DATE_EPOCH: reproduciblePackageEpoch,
                },
                stdio: 'inherit',
            },
        );
        child.once('error', reject);
        child.once('exit', (exitCode, signal) => {
            if (signal !== null) {
                reject(new Error(`vsce was terminated by signal ${signal}.`));
                return;
            }
            if (exitCode !== 0) {
                reject(new Error(`vsce exited with status ${exitCode}.`));
                return;
            }
            resolve();
        });
    });
}

async function verifyPackage(
    outputPath,
    expectedManifest,
    forbiddenBuildDirectories,
) {
    const packaged = await readVSIXPackage(outputPath);
    const fields = ['publisher', 'name', 'version'];
    for (const field of fields) {
        if (packaged.manifest[field] !== expectedManifest[field]) {
            throw new Error(
                `${path.basename(outputPath)} has ${field} `
                + `'${packaged.manifest[field]}', expected '${expectedManifest[field]}'.`,
            );
        }
    }

    const bundlePath = 'extension/dist/extension.js';
    const packagedFiles = await readZip(
        outputPath,
        (fileName) => fileName === bundlePath,
    );
    const bundle = packagedFiles.get(bundlePath);
    if (bundle === undefined) {
        throw new Error(`${path.basename(outputPath)} has no extension bundle.`);
    }
    const bundleText = bundle.toString('utf8');
    for (const directory of forbiddenBuildDirectories) {
        const normalizedDirectory = directory.split(path.sep).join('/');
        if (
            bundleText.includes(directory)
            || bundleText.includes(normalizedDirectory)
        ) {
            throw new Error(
                `${path.basename(outputPath)} contains build path '${directory}'.`,
            );
        }
    }
}

async function packageRegistries(outputDirectory, packages) {
    const canonicalManifest = JSON.parse(
        await readFile(path.join(extensionDirectory, 'package.json'), 'utf8'),
    );
    const stagingDirectory = await mkdtemp(
        path.join(os.tmpdir(), 'dynlex-extension-packages-'),
    );

    await mkdir(outputDirectory, { recursive: true });
    try {
        for (const registryPackage of packages) {
            const registryRoot = path.join(
                stagingDirectory,
                registryPackage.registry,
            );
            const stagedExtensionDirectory = path.join(
                registryRoot,
                'vscode-extension',
            );
            const stagedManifest = buildRegistryManifest(
                canonicalManifest,
                registryPackage,
            );
            const outputPath = path.join(
                outputDirectory,
                artifactFileName(stagedManifest, registryPackage),
            );

            await mkdir(registryRoot, { recursive: true });
            await copyFile(
                path.join(repositoryDirectory, 'LICENSE.md'),
                path.join(registryRoot, 'LICENSE.md'),
            );
            await cp(extensionDirectory, stagedExtensionDirectory, {
                recursive: true,
                filter: shouldCopySource,
            });
            await copyProductionDependencies(stagedExtensionDirectory);
            await writeFile(
                path.join(stagedExtensionDirectory, 'package.json'),
                `${JSON.stringify(stagedManifest, null, 2)}\n`,
            );
            await rm(outputPath, { force: true });
            await runVscePackage(stagedExtensionDirectory, outputPath);
            await verifyPackage(
                outputPath,
                stagedManifest,
                [extensionDirectory, stagingDirectory],
            );
        }
    } finally {
        await rm(stagingDirectory, { recursive: true, force: true });
    }
}

const invokedPath = process.argv[1]
    ? pathToFileURL(path.resolve(process.argv[1])).href
    : '';
if (import.meta.url === invokedPath) {
    const options = parseArguments(process.argv.slice(2));
    await packageRegistries(options.outputDirectory, options.packages);
}
