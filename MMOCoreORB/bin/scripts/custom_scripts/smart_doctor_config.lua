-- MMOCoreORB/bin/scripts/custom_scripts/smart_doctor_config.lua

local SmartDoctorConfig = {}

-- Pricing
SmartDoctorConfig.price = 25000

-- Timing: 6 steps x 4.5s = ~27s
SmartDoctorConfig.step_delay_ms = 4500
SmartDoctorConfig.confirm_timeout_ms = 15000
SmartDoctorConfig.pause_grace_ms = 6000

-- Behavior
SmartDoctorConfig.max_range = 10
SmartDoctorConfig.face_target = true
SmartDoctorConfig.max_queue_length = 10
SmartDoctorConfig.min_seconds_between_requests = 2

-- Buff order
SmartDoctorConfig.buff_steps = {
  "health", "strength", "constitution", "action", "quickness", "stamina"
}

-- Optional: name shown on NPC
SmartDoctorConfig.doctor_custom_name = "Doc Buffer"

-- Spawn points (server start)
-- fields: planet, x, z, y, heading, cell (optional), customName(optional)
SmartDoctorConfig.spawn_points = {
  {
    planet = "corellia",
    x = -153,
    z = 28,
    y = -4723,
    heading = 90,
    cell = 0,
    customName = "Doc Buffer"
  }
}

return SmartDoctorConfig