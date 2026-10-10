#!/usr/bin/env python3
"""Verify the shipping plugin in an isolated Unreal host; commit only after success."""
import argparse
import json
import os
from pathlib import Path
import platform
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def run(command, log=None):
    if log:
        with log.open('w') as output:
            subprocess.run(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT, check=True)
    else:
        subprocess.run(command, cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--commit-message-file', type=Path)
    parser.add_argument('--tests', default='Inkpot.Prosetta')
    args = parser.parse_args()
    if args.commit_message_file and not args.commit_message_file.is_file():
        parser.error('Commit message file must exist.')
    system = platform.system()
    default = '/Users/Shared/Epic Games/UE_5.8' if system == 'Darwin' else 'C:/Program Files/Epic Games/UE_5.8'
    engine = Path(os.environ.get('UE_ROOT', default))
    host = ROOT / 'Saved/VerificationHost'
    source = host / 'Source/InkpotVerification'
    source.mkdir(parents=True, exist_ok=True)
    plugins = host / 'Plugins'
    plugins.mkdir(exist_ok=True)
    plugin = plugins / 'Inkpot'
    if not plugin.exists():
        plugin.symlink_to(ROOT, target_is_directory=True)
    project = host / 'InkpotVerification.uproject'
    project.write_text(json.dumps({'FileVersion': 3, 'Modules': [{'Name': 'InkpotVerification', 'Type': 'Runtime', 'LoadingPhase': 'Default'}], 'Plugins': [{'Name': 'Inkpot', 'Enabled': True}]}))
    (source / 'InkpotVerification.Build.cs').write_text('using UnrealBuildTool; public class InkpotVerification : ModuleRules { public InkpotVerification(ReadOnlyTargetRules Target) : base(Target) { PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs; PublicDependencyModuleNames.AddRange(new string[] {"Core", "CoreUObject", "Engine"}); } }')
    (source / 'InkpotVerification.cpp').write_text('#include "Modules/ModuleManager.h"\nIMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, InkpotVerification, "InkpotVerification");\n')
    (host / 'Source/InkpotVerificationEditor.Target.cs').write_text('using UnrealBuildTool; public class InkpotVerificationEditorTarget : TargetRules { public InkpotVerificationEditorTarget(TargetInfo Target) : base(Target) { Type = TargetType.Editor; DefaultBuildSettings = BuildSettingsVersion.Latest; IncludeOrderVersion = EngineIncludeOrderVersion.Latest; ExtraModuleNames.Add("InkpotVerification"); } }')
    report = ROOT / 'Saved/Automation/agent-verify'
    report.mkdir(parents=True, exist_ok=True)
    run(['git', 'diff', '--check'])
    run(['git', 'diff', '--cached', '--check'])
    if system == 'Darwin':
        build = [str(engine / 'Engine/Build/BatchFiles/Mac/Build.sh')]
        editor = engine / 'Engine/Binaries/Mac/UnrealEditor-Cmd'
        target = 'Mac'
    else:
        build = ['cmd.exe', '/c', str(engine / 'Engine/Build/BatchFiles/Build.bat')]
        editor = engine / 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
        target = 'Win64'
    run([*build, 'InkpotVerificationEditor', target, 'Development', str(project), '-WaitMutex', '-NoHotReloadFromIDE', '-NoUBA'], report / 'build.log')
    index = report / 'index.json'
    index.unlink(missing_ok=True)
    run([str(editor), str(project), f'-ExecCmds=Automation RunTests {args.tests}', '-TestExit=Automation Test Queue Empty', f'-ReportExportPath={report}', '-unattended', '-nopause', '-nosplash', '-nullrhi', '-stdout', '-FullStdOutLogOutput'], report / 'tests.log')
    result = json.loads(index.read_text(encoding='utf-8-sig'))
    tests = result.get('tests', [])
    if not tests or result.get('failed', 0) or result.get('notRun', 0) or any(test.get('state') != 'Success' for test in tests):
        raise RuntimeError('Automation failed, skipped tests, or did not run. Inspect Saved/Automation/agent-verify.')
    print(f"agent:verify passed: {len(tests)} tests, no failures or skips.")
    if args.commit_message_file:
        run(['git', 'commit', '--file', str(args.commit_message_file.resolve())])


if __name__ == '__main__':
    main()
