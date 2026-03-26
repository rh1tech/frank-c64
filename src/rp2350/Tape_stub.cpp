/*
 *  Tape_stub.cpp - Stub tape implementation for RP2350
 *
 *  FRANK C64 - Commodore 64 Emulator for RP2350
 *  Copyright (c) 2024-2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Provides empty implementations to satisfy linker until full tape support is added.
 */

#include "../sysdeps.h"
#include "../Tape.h"
#include <cstring>

// Stub implementations - tape not yet supported on RP2350

Tape::Tape(MOS6526 *cia) : the_cia(cia)
{
    the_file = nullptr;
    tap_version = 0;
    header_size = 0;
    data_size = 0;
    write_protected = false;
    file_extended = false;
    current_pos = 0;
    motor_on = false;
    button_state = TapeState::Stop;
    drive_state = TapeState::Stop;
    read_pulse_length = -1;
    write_cycle = 0;
    first_write_pulse = false;
}

Tape::~Tape()
{
    close_image_file();
}

void Tape::Reset()
{
    motor_on = false;
    button_state = TapeState::Stop;
    drive_state = TapeState::Stop;
    read_pulse_length = -1;
}

void Tape::GetState(TapeSaveState *s) const
{
    s->current_pos = current_pos;
    s->read_pulse_length = read_pulse_length;
    s->write_cycle = write_cycle;
    s->first_write_pulse = first_write_pulse;
    s->button_state = button_state;
}

void Tape::SetState(const TapeSaveState *s)
{
    current_pos = s->current_pos;
    read_pulse_length = s->read_pulse_length;
    write_cycle = s->write_cycle;
    first_write_pulse = s->first_write_pulse;
    button_state = s->button_state;
}

void Tape::NewPrefs(const Prefs *prefs)
{
    // Nothing to do - no tape file handling yet
}

void Tape::SetMotor(bool on)
{
    motor_on = on;
    set_drive_state();
}

void Tape::SetButtons(TapeState state)
{
    button_state = state;
    set_drive_state();
}

void Tape::Rewind()
{
    // Nothing to do
}

void Tape::Forward()
{
    // Nothing to do
}

int Tape::TapePosition() const
{
    return 0;
}

void Tape::WritePulse(uint32_t cycle)
{
    // Nothing to do - no tape write support yet
}

void Tape::set_drive_state()
{
    if (motor_on && button_state == TapeState::Play) {
        drive_state = TapeState::Play;
    } else if (motor_on && button_state == TapeState::Record) {
        drive_state = TapeState::Record;
    } else {
        drive_state = TapeState::Stop;
    }
}

void Tape::open_image_file(const std::string &filepath)
{
    // Not implemented yet
}

void Tape::close_image_file()
{
    if (the_file) {
        fclose(the_file);
        the_file = nullptr;
    }
}

void Tape::schedule_read_pulse()
{
    // Not implemented yet
}

void Tape::trigger_read_pulse()
{
    // Not implemented yet
}

// IsTapeImageFile stub - required by IEC.cpp
bool IsTapeImageFile(const std::string &path, const uint8_t *header, long size)
{
    return false;  // No tape support yet
}

// CreateTapeImageFile stub
bool CreateTapeImageFile(const std::string &path)
{
    return false;  // No tape support yet
}

// IsArchFile stub - required by IEC.cpp (for T64 archives)
bool IsArchFile(const std::string &path, const uint8_t *header, long size)
{
    return false;  // No T64 archive support yet
}
