-- MMOCoreORB/bin/scripts/custom_scripts/smart_doctor_config.lua

SmartDoctorConfig = SmartDoctorConfig or {}

SmartDoctorConfig.price = SmartDoctorConfig.price or 5000

-- Timing: 6 steps x 4.5s = ~27s
SmartDoctorConfig.step_delay_ms = SmartDoctorConfig.step_delay_ms or 4500
SmartDoctorConfig.confirm_timeout_ms = SmartDoctorConfig.confirm_timeout_ms or 15000
SmartDoctorConfig.pause_grace_ms = SmartDoctorConfig.pause_grace_ms or 8000

SmartDoctorConfig.max_range = SmartDoctorConfig.max_range or 10
SmartDoctorConfig.face_target = (SmartDoctorConfig.face_target ~= false)
SmartDoctorConfig.max_queue_length = SmartDoctorConfig.max_queue_length or 10
SmartDoctorConfig.min_seconds_between_requests = SmartDoctorConfig.min_seconds_between_requests or 2

SmartDoctorConfig.buff_steps = SmartDoctorConfig.buff_steps or {
    "health", "strength", "constitution", "action", "quickness", "stamina"
}
SmartDoctorConfig.total_steps = SmartDoctorConfig.total_steps or #SmartDoctorConfig.buff_steps

SmartDoctorConfig.doctor_custom_name = SmartDoctorConfig.doctor_custom_name or "Doctor"
SmartDoctorConfig._spawn_registry_prefix = SmartDoctorConfig._spawn_registry_prefix or "SmartDoctorBuffer:spawned:"

-- Spawn points (server start)
-- fields: key, planet, x, z, y, heading, cell (optional), customName(optional)
SmartDoctorConfig.spawn_points = SmartDoctorConfig.spawn_points or {
    {
        key = "coronet_medcenter",
        planet = "corellia",
        x = -18.54,
        z = 0.26,
        y = 3.33,
        heading = 90,
        cell = 1855535,
        customName = "Doctor Buffer"
    },
    {
        key = "moenia_medcenter",
        planet = "naboo",
        x = -18.57,
        z = 0.26,
        y = 3.02,
        heading = 0,
        cell = 1717506,
        customName = "Doctor Buffer"
    },
    {
        key = "theed_medcenter",
        planet = "naboo",
        x = -18.46,
        z = 0.26,
        y = 3.39,
        heading = 0,
        cell = 1697364,
        customName = "Doctor Buffer"
    },
    {
        key = "mos_eisley_medcenter",
        planet = "tatooine",
        x = 7.45,
        z = 0.18,
        y = 2.29,
        heading = 180,
        cell = 9655496,
        customName = "Doctor Buffer"
    }
}

return SmartDoctorConfig
