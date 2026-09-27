#include "desktop_prediction_reference.hpp"
#include "desktop_timewarp_reference.hpp"
#include <iomanip>
#include <iostream>

static Eigen::Quaterniond read_quaternion() {
    double w,x,y,z; std::cin >> w >> x >> y >> z;
    return {w,x,y,z};
}
static Eigen::Vector3d read_vector() {
    Eigen::Vector3d value;
    for (int i=0;i<3;++i) std::cin >> value(i);
    return value;
}
int main() {
    std::cout << std::setprecision(17);
    char type;
    while (std::cin >> type) {
        if (type == 'P') {
            double dt; std::cin >> dt;
            desktop_reference::Input input;
            input.pos=read_vector(); input.vel=read_vector(); input.quat=read_quaternion();
            input.w_hat=read_vector(); input.a_hat=read_vector();
            input.w_hat2=read_vector(); input.a_hat2=read_vector();
            const Eigen::Quaternionf offset=read_quaternion().cast<float>();
            Eigen::Matrix<double,13,1> result;
            if (dt == 0.0) {
                result.setZero(); result.head<4>() << input.quat.x(),input.quat.y(),input.quat.z(),input.quat.w();
                result.segment<3>(4)=input.pos; result.segment<3>(7)=input.vel;
            } else result=desktop_reference::Prediction::predict_mean_rk4(input,dt);
            for (int i=0;i<13;++i) std::cout << result(i) << ' ';
            // Desktop correct_pose casts to float before the axis conversion.
            const Eigen::Quaternionf q{float(result(3)),float(result(0)),float(result(1)),float(result(2))};
            const Eigen::Quaternionf corrected=Eigen::Quaternionf{q.w(),-q.y(),q.z(),-q.x()}*offset;
            std::cout << -float(result(5)) << ' ' << float(result(6)) << ' ' << -float(result(4)) << ' '
                      << corrected.w() << ' ' << corrected.x() << ' ' << corrected.y() << ' ' << corrected.z() << '\n';
        } else if (type == 'T') {
            const Eigen::Quaternionf render=read_quaternion().cast<float>();
            const Eigen::Quaternionf latest=read_quaternion().cast<float>();
            Eigen::Matrix4f projection,render_view=Eigen::Matrix4f::Identity(),new_view=Eigen::Matrix4f::Identity(),transform;
            desktop_reference::math_util::projection_fov(&projection,45,45,45,45,0.1f,20.0f);
            render_view.block<3,3>(0,0)=render.toRotationMatrix();
            new_view.block<3,3>(0,0)=latest.toRotationMatrix();
            desktop_reference::Timewarp::calculate_timewarp_transform(transform,projection,render_view,new_view);
            for (int row=0;row<4;++row) for (int col=0;col<4;++col)
                std::cout << transform(row,col) << (row==3 && col==3?'\n':' ');
        } else return 2;
        if (!std::cin) return 3;
    }
}
