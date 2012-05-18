require 'orocos'
require 'vizkit'
require 'optparse'
require File.join(File.dirname(__FILE__), 'params')

include Orocos
Orocos.initialize

options = {}
components = ["sonar.0.0.log", "orientation_estimator.0.log", "avalon_back_base_control.0.log"]
dir = "~/logs/sauc-e_first_day/20110704-1615/"

files = components.map do |comp|
    File.join(dir, "#{comp}")
end

log = Orocos::Log::Replay.open(*files)

view3d = Vizkit.default_loader.create_widget 'vizkit::Vizkit3DWidget'
view3d.show_grid = false
view3d.show
ep = view3d.createPlugin("RigidBodyStateVisualization")
mon = view3d.createPlugin("uw_localization_monitor", "MonitorVisualization")

Orocos.run "uwv_dynamic_model", "uw_particle_localization_test", "sonar_feature_estimator", :wait => 999 do
    Orocos.log_all_ports

    sonar = log.task 'sonar'
    state = log.task 'orientation_estimator'
    motion = log.task 'motion_control'
    pos = TaskContext.get 'uw_particle_localization'
    feature = TaskContext.get 'sonar_feature_estimator'
    mm = TaskContext.get 'uwv_dynamic_model'

    sonar.BaseScan.connect_to feature.sonar_input
    motion.hbridge_commands.connect_to mm.thrusterinput
    state.orientation_samples.connect_to pos.orientation_samples
    mm.uwvstate.connect_to pos.speed_samples
    feature.new_feature.connect_to pos.laser_samples

    feature.derivative_history_length = 3

    params = mm.uwv_param
    AvalonModelParameters::initialize_vehicle_parameters(params)
    mm.uwv_param = params

    pos.init_position = [0.0,-4.0, 0.0]
    pos.init_variance = [10.0, 10.0, 0.0]

    pos.static_motion_covariance = [4.0,0.0,0.0,  0.0,4.0,0.0,  0.0,0.0,0.0]
    pos.pure_random_motion = true

    pos.particle_number = 20
    pos.minimum_depth = 0.0
    pos.minimum_perceptions = 2
    pos.effective_sample_size_threshold = 0.8
    pos.particle_interspersal_ratio = 0.0
    pos.sonar_maximum_distance = 13.0
    pos.sonar_minimum_distance = 2.0
    pos.sonar_covariance = 2.0

    pos.yaml_map = File.join("..", "maps", "nurc.yml")

    Vizkit.display pos
    Vizkit.display mm
    Vizkit.display feature
    
    Vizkit.connect_port_to 'uw_particle_localization', 'environment', :pull => false, :update_frequency => 33 do |sample, _|
        mon.updateEnvironment(sample)
        sample
    end

    Vizkit.connect_port_to 'uw_particle_localization', 'particles', :pull => false, :update_frequency => 33 do |sample, _|
        mon.updateParticleSet(sample)
        sample
    end

    Vizkit.connect_port_to 'uw_particle_localization', 'debug_sonar', :pull => false, :update_frequency => 33 do |sample, _|
        mon.updateParticleInfo(sample)
        sample
    end

    Vizkit.connect_port_to 'uw_particle_localization', 'orientation_samples', :pull => false, :update_frequency => 33 do |sample, _|
        ep.updateRigidBodyState(sample)
        sample
    end

    mm.configure
    pos.configure
    feature.configure
    pos.start
    feature.start
    mm.start

    Vizkit.control log 
    Vizkit.exec
end

