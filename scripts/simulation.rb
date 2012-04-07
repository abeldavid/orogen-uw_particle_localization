require 'vizkit'

include Orocos
Orocos.initialize

view3d = Vizkit.default_loader.create_widget 'vizkit::Vizkit3DWidget'
gt = view3d.createPlugin("RigidBodyStateVisualization")
ep = view3d.createPlugin("RigidBodyStateVisualization")
traj = view3d.createPlugin("TrajectoryVisualization")
laserviz = view3d.createPlugin 'uw_localization_laserscan', 'LaserScanVisualization'
viz = view3d.createPlugin("uw_localization_particle", "ParticleVisualization")
landmark = view3d.createPlugin("uw_localization_landmark", "LandmarkVisualization")
view3d.show

Orocos.run "uw_particle_localization_test", "sonar_feature_estimator", :wait => 999 do
#    sim = TaskContext.get 'avalon_simulation'
#    sonar = TaskContext.get 'sonar'
#    state = TaskContext.get 'state_estimator'
    pos = TaskContext.get 'uw_particle_localization'
    feature = Orocos::TaskContext.get 'sonar_feature_estimator'

#    sonar.sonar_beam.connect_to feature.sonar_input
#    state.pose_samples.connect_to pos.orientation_samples
#    state.pose_samples.connect_to pos.speed_samples

#    feature.derivative_history_length = 1

    pos.init_position = [0.0, 0.0, 0.0]
    pos.init_covariance = [4.0, 0.0, 0.0,
                           0.0, 4.0, 0.0,
                           0.0, 0.0, 4.0]

    pos.particle_number = 100

    pos.sonar_covariance = [1.0, 0.0, 0.0,
                            0.0, 1.0, 0.0,
                            0.0, 0.0, 1.0]

    pos.yaml_map = File.join("..", "maps", "studiobad.yml")

    pos.configure
    feature.configure

    Vizkit.connect_port_to 'uw_particle_localization', 'particles', :type => :buffer, :size => 100, :pull => false, :update_frequency => 33 do |sample, name|
        viz.updateParticles(sample)
        sample
    end

    Vizkit.connect_port_to 'uw_particle_localization', 'landmarks', :type => :buffer, :size => 100, :pull => false, :update_frequency => 33 do |sample, name|
        landmark.updateMap(sample)
        sample
    end


    pos.start
##    feature.start

    Vizkit.exec
end
