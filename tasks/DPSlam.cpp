#include "DPSlam.hpp"

using namespace uw_localization;

DPSlam::DPSlam(){
 
  map = 0;
  lastAngle = NAN;
  sumAngle = 0.0;
}

DPSlam::~DPSlam(){
   delete map;
   map = 0;
}

void DPSlam::init(base::Vector2d position, base::Vector2d span, double resolution, const FilterConfig config){
  
  this->config = config;
  this->position = position;
  this->span = span;
  delete map;
  std::cout << "Center: " << position << std::endl;
  map = new DPMap(position, span, resolution);  
  map->initGrid();
  
  map->initDepthObstacleConfig(-8.0, 0.0, 2.0);
  
}

void DPSlam::initalize_statics(NodeMap *map){
  
  node_map = map;
  this->map->initalizeStatics(map);
}

void DPSlam::observeDepth(const base::Vector3d &pos, const base::Matrix3d &pos_covar, const double &depth){
  
  double var = pos_covar.norm();
  
  map->setStaticDepth(pos.x(), pos.y(), depth, var);  
  
}

double DPSlam::observe(PoseSlamParticle &X, const double &depth){
  
  Eigen::Vector2d pos = map->getGridCoord(X.p_position.x(), X.p_position.y());
  
  //Search for correspondig cell
  std::map< std::pair<double, double> , std::pair<Eigen::Vector2d,int64_t > >::iterator it 
          = X.depth_cells.find( std::make_pair(pos.x(), pos.y()) );

  if(it != X.depth_cells.end()){  

      int64_t id = map->setDepth(pos.x(), pos.y(), depth, config.echosounder_variance , it->second.second);
      
      if(id != 0 && id != it->second.second){
        it->second.second = id;
      }
      
      if(id == 0){
        X.depth_cells.erase(it);
      }
      
      return X.main_confidence;    
  }
  
  //We found no match, set new feature!
  int64_t id = map->setDepth(pos.x(), pos.y(), depth, config.echosounder_variance , 0);
 
  if(id != 0)
    X.depth_cells[ std::make_pair(pos.x(), pos.y())  ] = std::make_pair(pos, id);
  
  return X.main_confidence;
  
}


double DPSlam::observe(PoseSlamParticle &X, const sonar_detectors::ObstacleFeatures& Z, double vehicle_yaw, double vehicle_depth){
  std::vector<Eigen::Vector2d> cells = map->getGridCells( Eigen::Vector2d(X.p_position.x(), X.p_position.y()), Z.angle + vehicle_yaw,
                                                         config.feature_observation_minimum_range, config.feature_observation_range, true);
  
  Eigen::AngleAxis<double> sonar_yaw(Z.angle, Eigen::Vector3d::UnitZ()); 
  Eigen::AngleAxis<double> abs_yaw(vehicle_yaw, Eigen::Vector3d::UnitZ());    
  Eigen::Affine3d SonarToAvalon(config.sonarToAvalon);
  
  Eigen::Vector2d pos2d(X.p_position.x(), X.p_position.y() );
  
  reduceFeatures(vehicle_yaw + Z.angle);
  
  
  std::list<double> distances; //Save the observed distances;
  std::list< std::pair<double, double> > distances_cells;  //Distances of the grid map, as pair (distance, confidence)
  
  if(!config.use_mapping_only){
  
    std::list< std::pair<Eigen::Vector2d, double > > observed_cells = map->getObservedCells(cells, X.obstacle_cells);
    
    for(std::list< std::pair<Eigen::Vector2d, double > >::iterator it = observed_cells.begin(); it != observed_cells.end(); it++){
      
      distances_cells.push_back( std::make_pair ( (it->first -  pos2d).norm(), it->second) );
      
    }
  }
  
  //std::cout << "Observe " << Z.features.size() << std::endl;
 int feature_count = 0;
 for(std::vector<sonar_detectors::ObstacleFeature>::const_iterator it_f = Z.features.begin(); it_f != Z.features.end(); it_f++){
      
      double dist = it_f->range / 1000.0;
      
      //std::cout << "Dist: " << dist << "Thresholds: " << config.sonar_minimum_distance << " " << config.sonar_maximum_distance << " " << config.feature_observation_range << std::endl;
      
      //Is feature in valid range??
      if(dist < config.sonar_minimum_distance ||
        dist > config.sonar_maximum_distance ||
        dist < config.feature_observation_minimum_range ||
        dist > config.feature_observation_range){
        //std::cout << "Obstacle out of range" << std::endl;
       continue; 
      }
      
      
      //Calculate feature in world frame
      Eigen::Vector3d RelativeZ = sonar_yaw * SonarToAvalon * base::Vector3d(dist, 0.0, 0.0);
      Eigen::Vector3d real_pos = (abs_yaw * RelativeZ) + X.p_position;
      
      double vertical_span = dist * std::sin(config.sonar_vertical_angle / 2.0);
      
      //If feature inside the map?
      if(!node_map->belongsToWorld(real_pos)){
        //std::cout << "Out of map" << std::endl;
        continue; //Feature is outside, ignore it!
      
      }
      
      Eigen::Vector2d feature_discrete = map->getGridCoord(real_pos.x(), real_pos.y() );
      
      //Check, if feature is in valid coordinates. If feature is outside the grid, values could be nan
      if(std::isnan(feature_discrete.x())){
        //std::cout << "Feature out of map" << std::endl;
        continue;
        
      }
        
      distances.push_back(dist);
      
      //Search for coresponding grid cells and delete them
      for(std::vector<Eigen::Vector2d>::iterator it_c = cells.begin(); it_c != cells.end(); it_c++){
        
        if(feature_discrete == *it_c){
          
          cells.erase(it_c);
          break;       
          
        }        
      }     
      
      bool found_match = false;
      //std::cout << "Obstacle " << feature_discrete.transpose() << std::endl;
      
      std::map< std::pair<double, double> , std::pair<Eigen::Vector2d,int64_t > >::iterator it 
          = X.obstacle_cells.find( std::make_pair( feature_discrete.x(), feature_discrete.y()) ); 
      
      if(it != X.obstacle_cells.end()){        

          int64_t id = map->setObstacle(feature_discrete.x(), feature_discrete.y(), true,
                                        config.feature_confidence , vehicle_depth - vertical_span,
                                        vehicle_depth + vertical_span, it->second.second);
           //std::cout << "UPdate Obstacle" << std::endl;
          
          //We have got a valid feature
          if(id != 0){
            it->second.second = id;
            feature_count++;
          }
          else{ //Feature could not be set, maybe it was deleted or invalid
            X.obstacle_cells.erase(it);
          }
          
          found_match = true;       
                
      } 
      
      if(!found_match){
        //std::cout << "Create new Obstacle" << std::endl;
        int64_t id = map->setObstacle(feature_discrete.x(), feature_discrete.y(), true,
                                      config.feature_confidence, vehicle_depth - vertical_span, vehicle_depth + vertical_span, 0);
        feature_count++;
        
        if(id != 0){
          X.obstacle_cells[ std::make_pair(feature_discrete.x(), feature_discrete.y()) ] = std::make_pair(feature_discrete, id);
        }
      }
  }
  
  //handle empty cells
  //There is no observation for the cells, update cells and lower their confidence
  for(std::vector<Eigen::Vector2d>::iterator it = cells.begin(); it != cells.end(); it++){
    
    double dist = std::sqrt( std::pow( it->x() - X.p_position.x(), 2.0)  + std::pow( it->y() - X.p_position.y(), 2.0 )  );
    double vertical_span = dist * std::sin(config.sonar_vertical_angle);
    
    
    //search for correspondig observations
    std::map< std::pair<double, double> , std::pair<Eigen::Vector2d, int64_t> >::iterator it_o
        = X.obstacle_cells.find( std::make_pair(it->x(), it->y() ) );
    
    //we found a correpondig feature
    if(it_o != X.obstacle_cells.end() ){      
        
        //Feature is inside our observation range -> update confidence
        if(dist <= config.feature_observation_range){
        
          int64_t id = map->setObstacle(it->x(), it->y(), false,
                                      config.feature_empty_cell_confidence, vehicle_depth - vertical_span,
                                      vehicle_depth + vertical_span, it_o->second.second);
        
          if(id != 0){
            it_o->second.second = id;          
          }
          else{
            X.obstacle_cells.erase(it_o);
          }
        
        }else{//Feature is outside observation rannge -> mark it, so we now, that it is still used
          
          map->touchObstacleFeature(it->x(), it->y(), it_o->second.second);
          
        }
           
    }
    
  } 
    
  //std::cout << "Set " << feature_count << " features" << std::endl;
  
  //Rate particle
  
  if(config.use_mapping_only)
    return 0.0;
    
    
  return rateParticle(distances, distances_cells);
  
    
  //return X.main_confidence;
  
}

double DPSlam::rateParticle(std::list<double> &distances, std::list<std::pair<double, double> > &distances_cells){

  std::list< std::pair<double, double> > diffs; //list of distance differences as pair: (difference, confidence)
  
  //Iterate through meassured distances, and find corresponding feature in map
  for(std::list<double>::iterator it = distances.begin(); it != distances.end(); it++){    
    
    double min_diff = INFINITY;
    std::list< std::pair<double, double > >::iterator min_it;
    
    //Search for best corrsponding feature
    for(std::list<std::pair<double, double > >::iterator it_c = distances_cells.begin(); it_c != distances_cells.end(); it_c++){
      
      double diff = std::fabs( *it - it_c->first);
      
      if(diff < min_diff){
        min_it = it_c;
        min_diff = diff;
        
      }
      
    }
    
    //we found at least one feature -> save distance difference
    if(min_diff != INFINITY){
      diffs.push_back( std::make_pair(min_diff, min_it->second ) );
      distances_cells.erase(min_it);
      
    }
    
  }  
  
  double prob = 0.0;
  double sum_weight = 0.0;
  
  for(std::list< std::pair<double, double > >::iterator it = diffs.begin(); it != diffs.end(); it++){
    prob += it->second * machine_learning::gaussian1d( 0.0, config.sonar_covariance, it->first );
    sum_weight += it->second;
    

  }
  
  if(sum_weight > 0.0){
    return prob / sum_weight;
  }

  return 0.0;
}



base::samples::Pointcloud DPSlam::getCloud(PoseSlamParticle &X){
  
  return map->getCloud(X.depth_cells, X.obstacle_cells, config.feature_output_confidence_threshold, config.feature_observation_count_threshold);
  
}

unsigned int DPSlam::getSimpleGrid(PoseSlamParticle &X, uw_localization::SimpleGrid &grid){
  
  return map->getSimpleGrid(grid, X.depth_cells, X.obstacle_cells, config.feature_output_confidence_threshold, config.feature_observation_count_threshold);
  
}


void DPSlam::reduceFeatures(double angle, double max_sum){
  
//  std::cout << "DP-Reduce features, angle: " << angle << " max_sum: " << max_sum << std::endl;
//  std::cout << "LastAngle: " << lastAngle << " sumAngle: " << sumAngle << std::endl;
  
  double diff = 0.0;
  
    //We need a new scan-angle
    if(lastAngle == angle)
      return;    
    
    if(std::isnan(lastAngle)){
      lastAngle = angle;
      return;
    }
    
    diff = std::fabs(lastAngle - angle);
    
    //Convert scan-difference to a range 0 - PI
    while(diff > M_PI)
      diff -= M_PI;
    
    diff = std::fabs(diff);
    
    sumAngle += diff;
    
//    std::cout << "Diff: " << diff << " sumAngle: " << sumAngle << std::endl;
    
    if(sumAngle > max_sum){
      map->reduceFeatures(config.feature_confidence_threshold, config.feature_observation_count_threshold );
      sumAngle = 0.0;
    }    

  lastAngle = angle;
    
}
