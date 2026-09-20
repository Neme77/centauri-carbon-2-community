# Centauri Carbon 2 Research & Technical Findings

This document records technical findings and implementation work from the **Centauri Carbon 2 Community Firmware** project maintained by **Neme77**.

The purpose is to provide a clear technical record of the work carried out on the **ELEGOO Centauri Carbon 2 (CC2)** and to distinguish this project's implementation from other independent Centauri community projects.

> This project is not affiliated with, endorsed by, or supported by ELEGOO. Other community projects may have independently researched the Centauri platform; this document describes the work and implementations developed in this repository.

## Project approach

Centauri Carbon 2 Community takes a deliberately conservative approach: extend the original CC2 Linux/Klipper-based platform while preserving the stock ELEGOO user experience wherever possible.

The objective is not to replace the complete ELEGOO software stack. Development has instead focused on restoring or exposing useful local capabilities while retaining the stock touchscreen, printer services and ELEGOO Matrix/cloud functionality.

## Firmware architecture and recovery

Research and testing confirmed the CC2 uses an A/B root filesystem layout. The community firmware workflow was developed around that architecture with recovery and reversibility treated as core requirements.

Development has included repeated real-hardware **stock → community → stock → community** installation cycles using ELEGOO firmware 02.01.00.00 as the verified stock base/recovery version.

The project provides firmware-building and recovery tooling rather than treating the modified image as a one-way installation.

## Root and SSH access

The project restores root/SSH access to the CC2 and provides tooling intended for development, diagnostics and advanced local integration.

Root access is not the end goal of the project: it is the foundation used to investigate the printer services and implement the local features documented below.

## LAN and WAN coexistence

One of the project's central findings was that local printer services could be retained while the printer remained in its normal WAN/cloud operating mode.

The resulting implementation allows local access and ELEGOO Matrix/cloud functionality to coexist rather than requiring the user to choose permanently between the two operating models.

Work in this area includes local HTTP and MQTT access and the authentication behaviour required by local clients such as OrcaSlicer and other integrations.

## Local webcam while preserving cloud operation

The CC2 camera service was investigated separately from the main printer service.

The community implementation keeps local webcam access available while the printer is operating with its cloud/WAN services enabled, without intentionally disabling the normal Matrix camera path.

This makes the camera usable by local dashboards and integrations while preserving the original ecosystem.

## Upload and local slicer integration

Testing identified differences between the stock software's handling of file upload and the underlying printer service.

The project enables local workflows used by tools such as OrcaSlicer and has investigated upload behaviour while the printer is already busy/printing.

## CC2 Control

**CC2 Control** is the lightweight web interface developed specifically for this project. It runs on the printer and exposes monitoring, diagnostics and selected protected controls without attempting to replace the stock touchscreen.

Current functionality includes live telemetry, camera access, temperature history, job information, system information, material presets and bed-mesh visualisation.

Development from V3.8 through V4.0 has primarily expanded this control layer while retaining the same underlying community-firmware foundation.

## Bed Mesh and four-screw leveling

The project exposes the CC2's stored bed mesh in both 2D and 3D views.

V4.0 also introduced four-screw load-cell bed tramming assistance. Measurements are performed at positions corresponding to the physical bed screws and are kept separate from the stored 11×11 bed mesh.

## Object Exclusion

V4.0 adds protected object-exclusion support for labelled multi-object prints, allowing an individual failed object to be excluded while the remaining objects continue printing when the print data supports it.

## BTT PandaBreath integration

The project includes a minimal read-only Moonraker-compatible bridge intended to expose the information required by **BTT PandaBreath** while avoiding the overhead and behavioural changes of replacing the stock CC2 software stack.

## Expert G-code terminal

V4.0 includes a temporary expert terminal unlock for advanced diagnostics and controlled G-code interaction.

The feature is intentionally treated as an expert function rather than an unrestricted default control surface.

## Relationship to other Centauri projects

**Centauri Carbon 2 Community Firmware is an independent project.**

It is not part of OpenCentauri and does not claim ownership of research independently performed by OpenCentauri or other members of the Centauri community.

The projects can pursue different technical goals. This repository documents an approach specifically focused on the **Centauri Carbon 2**, extending the original ELEGOO firmware while preserving the stock ecosystem wherever practical.

Where external research or code is incorporated in the future, it should be credited explicitly.

## Hardware validation

Development and validation have been performed on real Centauri Carbon 2 hardware rather than only through static firmware analysis.

Remote hardware validation and testing have also been contributed by **Barry Green**, including configurations and integration scenarios not available on the primary development machine.

Community reports provide additional real-world testing across multiple installations. Installation counts are based on voluntary feedback; the firmware does not contain usage telemetry.

## Research principles

The project follows a few practical rules:

- preserve stock functionality wherever possible;
- make modifications reversible;
- validate changes on real hardware;
- keep recovery paths documented;
- avoid claiming unsupported hardware or functionality;
- distinguish verified behaviour from experimental findings;
- credit independent external work when it is used.

## Further documentation

- [Community Firmware V4.0](FIRMWARE_V4_0.md)
- [V4.0 installation guide](INSTALL_V4_0.md)
- [CC2 Control](CC2_CONTROL.md)
- [Project README](../README.md)

The repository history and release notes provide the chronological record of shipped implementations and changes.
