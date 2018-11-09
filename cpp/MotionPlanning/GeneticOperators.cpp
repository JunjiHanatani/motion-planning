#include <vector>
#include <Eigen/Dense>
#include <iostream>
#include <fstream>
#include "utility.h"
#include "GeneticOperators.h"
#include "Vector3D.h"
#include "CollisionChecker.h"
#include "RecordLog.h"

using std::vector;
using std::cout;
using std::endl;;
using namespace Eigen;

int gen = 0;
int N_GEN = 10;
int N_POP = 30;
int N_SPLINE = 3;
int N_PTS = N_SPLINE * 2 + 1;
int N_JOINT = 2;
int N_ELITES = 1;
int N_TOURNAMENT = 3;
int const N_LAYERS = 5;
int AGE_GAP = 3;
int MAX_AGE[5] = {3, 6, 12, 24, 48};
double P_MUT = 0.3;
//double diversity_alpha = 0.5;
//double diversity_thresh = 1.0;

Robot robot[2];
vector<Individual> pops1[5];
vector<Individual> pops2[5];
//vector<Individual> pop2;

// -----------------------------------------------
// CREATE INITIAL POPULATION
// -----------------------------------------------

vector<Individual> createInitialPop(vector<double>start, vector<double>goal, int robotID){

    vector<Individual> pop(N_POP);

    //Create population
    for (int i=0; i<N_POP; i++){
        Individual ind;
        ind.path = createPath(start, goal);
        ind.fitness = 0.0;
        ind.distance = 0.0;
        ind.collision = 0;
        ind.diversity = 0.0;
        ind.age = 0;
        ind.robotID = robotID;
        pop[i] = ind;
    }

    return pop;
}

vector<vector<double>> createPath(vector<double>start, vector<double>goal){
    vector<double> mid_pt = {get_rand_range_dbl(-2.0*PI, 2.0*PI),
                             get_rand_range_dbl(-2.0*PI, 2.0*PI)};

    vector<vector<double>>goals;
    for (int i=-1; i<=1; i++){
        for (int j=-1; j<=1; j++){
            double add_pt0 = goal[0] + 2.0*PI*i;
            double add_pt1 = goal[1] + 2.0*PI*j;

            if (-2.0*PI < add_pt0 && add_pt0< 2.0*PI &&
                -2.0*PI < add_pt1 && add_pt1< 2.0*PI){
                vector<vector<double>> add_goal = {{add_pt0, add_pt1}};
                goals.insert(goals.end(), add_goal.begin(), add_goal.end());
            }
        }
    }

    vector<double> nearest_goal;
    double min_dist = 100.0;
    for (vector<double>goal:goals){
        vector<double>vec = sub(mid_pt, goal);
        double dist = calcNorm(vec);
        if (dist < min_dist){
            min_dist = dist;
            nearest_goal = goal;
        }
    }

    vector<vector<double>>path = {start, mid_pt, nearest_goal};
    vector<vector<double>>spline_path = ferguson_spline(path, N_SPLINE);
    return spline_path;
}


// -----------------------------------------------
// EVALUATION
// -----------------------------------------------

void evaluate(vector<Individual>&pop1, const vector<Individual> pop2){

    Individual best_ind = pop2[0];
    int num = pop1.size();
    calcDiversity(pop1);
    for (int i=0; i<num; i++) {
        if (pop1[i].fitness == 0.0){
            pop1[i].distance = calcPathLength(pop1[i].path) + calcPathLength(best_ind.path);
            pop1[i].collision = calcCollision(pop1[i], best_ind);
            pop1[i].fitness = (pop1[i].distance + pop1[i].collision * 100);
        }
        pop1[i].age += 1;
    }

}

double calcPathLength(vector<vector<double>> path){
    double distance=0.0;
    int num=path.size();

    for (int i=0; i<num-1; i++){
         distance += calcDistance(path[i], path[i+1]);
    }

    return distance;
}

int calcCollision(Individual ind1, Individual ind2){

    vector<vector<double>> spline1 = ferguson_spline(ind1.path, 10);
    vector<vector<double>> spline2 = ferguson_spline(ind2.path, 10);

    vector<vector<double>> config1, config2;
    vector<vector<double>> line1, line2;
    vector<double> pt1, pt2;
    int num = spline1.size();
    int collision_counter=0;

    for (int i=0; i<num; i++){

        config1 = robot[ind1.robotID].forward_kinematics(spline1[i]);
        config2 = robot[ind2.robotID].forward_kinematics(spline2[i]);

        // Line pair.
        for (int j=0; j<N_JOINT; j++){
            for (int k=0; k<N_JOINT; k++){
                line1={config1[j], config1[j+1]};
                line2={config2[k], config2[k+1]};

                // If rough check is true.
                if (isCollisionRoughCheck(line1, line2)){
                    // Line-Line check.
                    if (isCollisionLineLine(line1, line2)){
                        collision_counter += 1;
                        goto NEXT;
                    }
                    // Line-Circle check
                    if (isCollisionLineCircle(line1, line2[1], 0.1)){
                        collision_counter += 1;
                        goto NEXT;
                    }
                    // Line-Circle check
                    if (isCollisionLineCircle(line2, line1[1], 0.1)){
                        collision_counter += 1;
                        goto NEXT;
                    }
                }
            }
        }
        NEXT:;
    }

    return collision_counter;
}


// -----------------------------------------------
//  SORT
// -----------------------------------------------

bool operator<(const Individual& left, const Individual& right){
  return left.fitness< right.fitness ;
}

void sort_pop(vector<Individual> &pop){
    std::sort(pop.begin(), pop.end());
}


// -----------------------------------------------
// SELECTION
// -----------------------------------------------


vector<Individual> tournamentSelection(vector<Individual> const &pop){

    int n_offspring = N_POP - N_ELITES;
    vector<Individual> offspring(n_offspring);
    int rand_index;
    int min_index;

    for(int i=0; i<n_offspring; i++){
        min_index = N_POP;
        for (int j=0; j<N_TOURNAMENT; j++){
            rand_index = get_rand_range_int(0, N_POP-1);
            if (rand_index < min_index) min_index = rand_index;
        }
        offspring[i] = pop[min_index];
    }

    return offspring;
}

vector<Individual> rouletteSelection(vector<Individual> &pop){

    int n_offspring = N_POP - N_ELITES;
    vector<Individual> offspring;
    vector<double> rand_list(n_offspring);

    // Calculate sum of the fitness over all population.
    double sum_fitness = std::accumulate(pop.begin(), pop.end(), 0.0,
                     [](double sum, Individual& ind ){ return sum+1.0/ind.fitness; } );

    // Generate random list.
    for (int i=0; i<n_offspring; i++){
        rand_list[i] = get_rand_range_dbl(0.0, sum_fitness);
    }

    // Sort random_list.
    std::sort(rand_list.begin(), rand_list.end());
    std::reverse(rand_list.begin(), rand_list.end());

    double thresh = 0.0;
    for (int i=0; i<N_POP; i++){
        thresh += 1.0/pop[i].fitness;
        while(rand_list.size()!=0){
            double rand = rand_list.back();
            if (rand<thresh){
                offspring.push_back(pop[i]);
                rand_list.pop_back();
            }else{
                break;
            }
        }
    }
    return offspring;
}

vector<Individual> elitistSelection(vector<Individual> const &pop){
    vector<Individual> elites(N_ELITES);
    for (int i=0; i<N_ELITES; i++) elites[i] = pop[i];
    return elites;
}

vector<Individual> overageSelection(vector<Individual>offspring&){
    vector<Individual> overages;

    for (Individual ind:offspring){
        if(ind.age > MAX_AGE[id]){

        }
    }

}

// -----------------------------------------------
// MUTATION
// -----------------------------------------------

void mutation(
    std::function<Individual(Individual&)> mut_operator, vector<Individual> &pop){
    vector<Individual> add_pop;
    int num = pop.size();
    for (int i=0; i<num; i++){
        if (get_rand_range_dbl(0.0, 1.0) < P_MUT){
            Individual child = mut_operator(pop[i]);
            add_pop.insert(add_pop.end(), child);
        }

        //for(int j=0; j<5; j++){
        //    double p = pow(1.0 - P_MUT, j) * P_MUT;
        //    if (get_rand_range_dbl(0.0, 1.0) < p){
        //        mut_operator(pop[i]);
        //    }
        //}
    }
    pop.insert(pop.end(), add_pop.begin(), add_pop.end());
}

Individual mutNormal(Individual ind){

    for (int i=0; i<N_PTS-1; i++){
        if (get_rand_range_dbl(0.0, 1.0) < 0.3){
            ind.path[i][0] += get_rand_range_dbl(-0.1, 0.1);
            ind.path[i][1] += get_rand_range_dbl(-0.1, 0.1);
        }
    }
    Individual child = {ind.path, 0.0, 0.0, 0, 0.0, ind.age, ind.robotID};
    //path_check(ind, "mut0");
    //path_check(child, "mut1");
    return child;
}

// -----------------------------------------------
// CROSSOVER
// -----------------------------------------------

void crossover(std::function< vector<Individual>(Individual&, vector<Individual>&) > cx_operator,
               vector<Individual> &pop){

    int num = pop.size();
    vector<Individual> add_pop;
    //std::shuffle(pop.begin(), pop.end(), mt_engine);
    for(int i=0; i<num; i++){
        vector<Individual> children = cx_operator(pop[i], pop);
        if (children.size()!=0){
            add_pop.insert(add_pop.end(), children.begin(), children.end());
        }
    }

    pop.insert(pop.end(), add_pop.begin(), add_pop.end());
    adjust_num_pts(pop);

}

// --- Single-point crossover
vector<Individual> oneptcx(Individual &ind1, vector<Individual> &pop){
    int cut_pt1 = get_rand_range_int(1, N_PTS-2);
    int cut_pt2;
    vector<double> pt1 = ind1.path[cut_pt1];
    vector<double> pt2;
    Individual ind2;
    vector<Individual> children;


    // Search the nearest point.
    double min_dist=PI/10.0;
    int num=pop.size();
    for (int i=0; i<num; i++){
        if (pop[i].distance == ind1.distance) continue;
        for (int j=0; j<N_PTS; j++){
            pt2 = pop[i].path[j];
            double dist = calcDistance(pt1, pt2);
            if (dist < min_dist){
                min_dist = dist;
                ind2 = pop[i];
                cut_pt2 = j;
            }
        }
    }

    // Swap.
    if (ind2.path.size() != 0) {
        vector<vector<double>> path1 = ind1.path;
        vector<vector<double>> path2 = ind2.path;
        vector<vector<double>> child_path1;
        vector<vector<double>> child_path2;
        child_path1.insert(child_path1.end(), path1.begin(), path1.begin()+cut_pt1);
        child_path1.insert(child_path1.end(), path2.begin()+cut_pt2, path2.end());
        child_path2.insert(child_path2.end(), path2.begin(), path2.begin()+cut_pt2);
        child_path2.insert(child_path2.end(), path1.begin()+cut_pt1, path1.end());

        int age = std::max(ind1.age, ind2.age);
        // New individuals.
        Individual child1 = {child_path1, 0.0, 0.0, 0, 0.0, age, ind1.robotID};
        Individual child2 = {child_path2, 0.0, 0.0, 0, 0.0, age, ind2.robotID};
        children = {child1, child2};
        //path_check(ind1, "cx0");
        //path_check(ind2, "cx1");
        //path_check(child1, "cx2");
        //path_check(child2, "cx3");
    }
    return children;
}


// -----------------------------------------------

void calcDiversity(vector<Individual> &pop){
    //double sharing;
    int num = pop.size();

    // Initialize
    for (int i=0; i<num; i++) pop[i].diversity = 0.0;

    // Calculate distance between two paths.
    for (int i=0; i<num-1; i++){
        for (int j=i+1; j<num; j++){
            for (int k=0; k<N_PTS; k++){
                vector<double> pt1 = pop[i].path[k];
                vector<double> pt2 = pop[j].path[k];
                double dist = calcDistance(pt1, pt2);
                //if (dist < diversity_thresh){
                //    sharing = 1.0 - pow((dist/diversity_thresh), diversity_alpha);
                //}else{
                //    sharing = 0.0;
                //}
                //pop[i].diversity += sharing;
                //pop[j].diversity += sharing;
                pop[i].diversity += dist;
                pop[j].diversity += dist;

            }
        }
    }

    // Average distance.
    for (int i=0; i<num; i++) pop[i].diversity = pop[i].diversity/(num-1);

}

void adjust_num_pts(vector<Individual> &pop){

    int num = pop.size();
    for (int i=0; i<num; i++){
        int num = pop[i].path.size();
        if (num < N_PTS){
            int k = ((N_PTS - 1)/(num - 1)) + 1;
            pop[i].path = ferguson_spline(pop[i].path, k);
        }
        num = pop[i].path.size();
        while (num>N_PTS){
            int rand_index = get_rand_range_int(1, pop[i].path.size()-2);
            pop[i].path.erase(pop[i].path.begin() + rand_index);
            num = pop[i].path.size();
        }
    }
}

vector<vector<double>> ferguson_spline(vector<vector<double>>pts, int const num){

    const int n_pts = pts.size();

    // s matrix
    MatrixXd s_mat(num, 4);
    for (int i=0; i<num; i++){
        double s = (double)i/num;
        s_mat(i, 0) = s*s*s;
        s_mat(i, 1) = s*s;
        s_mat(i, 2) = s;
        s_mat(i, 3) = 1.0;
    }

    // Ferguson Spline Matrix
    Matrix<double, 4, 4> trans_matrix;
    trans_matrix << 2, 1, -2, 1,
                    -3, -2, 3, -1,
                    0, 1, 0, 0,
                    1, 0, 0, 0;

    // Velocity on each point
    MatrixXd velo(n_pts, 2);
    for (int i=0; i<n_pts; i++){
        for (int j=0; j<2; j++){
            if(i == n_pts-1){
                velo(i, j) = pts[i][j] - pts[i-1][j];
            }else if(i == 0){
                velo(i, j) = pts[i+1][j] - pts[i][j];
            }else{
                velo(i, j) = (pts[i+1][j] - pts[i-1][j])/2.0;
            }
        }
    }

    // Calc. spline
    vector<vector<double>> q;
    vector<vector<double>> q_add(num, vector<double>(2));
    Matrix<double, 4, 2> vec;
    MatrixXd q_add_mat(num, 2);

    for (int i=0; i<n_pts-1; i++){

        vec << pts[i][0], pts[i][1],
               velo(i,0), velo(i,1),
               pts[i+1][0], pts[i+1][1],
               velo(i+1,0), velo(i+1,1);

        q_add_mat = s_mat * trans_matrix * vec;

        // Matrix -> Vector conversion
        for (int i=0; i<num; i++){
            for (int j=0; j<2; j++){
                q_add[i][j] = q_add_mat(i, j);
            }
        }

        // Add vector.
        q.insert(q.end(), q_add.begin(), q_add.end());
    }
    q.insert(q.end(), pts.begin()+n_pts-1, pts.end());

    //std::ofstream ofs("./test.csv");
    //for (vector<double> pt:q){
    //    cout << pt[0] << " " << pt[1] << endl;
    //    ofs << pt[0] << "," << pt[1] << endl;
    //}

    return q;
}

