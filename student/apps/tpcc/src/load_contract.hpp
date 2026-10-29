#pragma once

#include <array>
#include <string_view>

namespace db::tpcc {

struct LoadTableContract final {
    std::string_view name;
    std::string_view create_sql;
};

struct LoadIndexContract final {
    std::string_view name;
    std::string_view create_sql;
};

inline constexpr std::array<LoadTableContract, 9> kLoadTables{
    LoadTableContract{
        .name = "warehouse",
        .create_sql = "create table warehouse (w_id int, w_name char(10), w_street_1 char(20), "
                      "w_street_2 char(20), w_city char(20), w_state char(2), w_zip char(9), "
                      "w_tax float, w_ytd float);",
    },
    LoadTableContract{
        .name = "district",
        .create_sql = "create table district (d_id int, d_w_id int, d_name char(10), "
                      "d_street_1 char(20), d_street_2 char(20), d_city char(20), d_state char(2), "
                      "d_zip char(9), d_tax float, d_ytd float, d_next_o_id int);",
    },
    LoadTableContract{
        .name = "customer",
        .create_sql = "create table customer (c_id int, c_d_id int, c_w_id int, c_first char(16), "
                      "c_middle char(2), c_last char(16), c_street_1 char(20), "
                      "c_street_2 char(20), c_city char(20), c_state char(2), c_zip char(9), "
                      "c_phone char(16), c_since char(30), c_credit char(2), c_credit_lim int, "
                      "c_discount float, c_balance float, c_ytd_payment float, c_payment_cnt int, "
                      "c_delivery_cnt int, c_data char(50));",
    },
    LoadTableContract{
        .name = "history",
        .create_sql = "create table history (h_c_id int, h_c_d_id int, h_c_w_id int, h_d_id int, "
                      "h_w_id int, h_date char(30), h_amount float, h_data char(24));",
    },
    LoadTableContract{
        .name = "new_orders",
        .create_sql = "create table new_orders (no_o_id int, no_d_id int, no_w_id int);",
    },
    LoadTableContract{
        .name = "orders",
        .create_sql = "create table orders (o_id int, o_d_id int, o_w_id int, o_c_id int, "
                      "o_entry_d char(30), o_carrier_id int, o_ol_cnt int, o_all_local int);",
    },
    LoadTableContract{
        .name = "order_line",
        .create_sql = "create table order_line (ol_o_id int, ol_d_id int, ol_w_id int, "
                      "ol_number int, ol_i_id int, ol_supply_w_id int, ol_delivery_d char(30), "
                      "ol_quantity int, ol_amount float, ol_dist_info char(24));",
    },
    LoadTableContract{
        .name = "item",
        .create_sql = "create table item (i_id int, i_im_id int, i_name char(24), i_price float, "
                      "i_data char(50));",
    },
    LoadTableContract{
        .name = "stock",
        .create_sql = "create table stock (s_i_id int, s_w_id int, s_quantity int, "
                      "s_dist_01 char(24), s_dist_02 char(24), s_dist_03 char(24), "
                      "s_dist_04 char(24), s_dist_05 char(24), s_dist_06 char(24), "
                      "s_dist_07 char(24), s_dist_08 char(24), s_dist_09 char(24), "
                      "s_dist_10 char(24), s_ytd float, s_order_cnt int, s_remote_cnt int, "
                      "s_data char(50));",
    },
};

inline constexpr std::array<LoadIndexContract, 10> kLoadIndexes{
    LoadIndexContract{.name = "warehouse_pk", .create_sql = "create index warehouse(w_id);"},
    LoadIndexContract{
        .name = "district_pk",
        .create_sql = "create index district(d_w_id, d_id);",
    },
    LoadIndexContract{
        .name = "customer_pk",
        .create_sql = "create index customer(c_w_id, c_d_id, c_id);",
    },
    LoadIndexContract{
        .name = "customer_last",
        .create_sql = "create index customer(c_w_id, c_d_id, c_last, c_id);",
    },
    LoadIndexContract{
        .name = "new_orders_pk",
        .create_sql = "create index new_orders(no_w_id, no_d_id, no_o_id);",
    },
    LoadIndexContract{
        .name = "orders_pk",
        .create_sql = "create index orders(o_w_id, o_d_id, o_id);",
    },
    LoadIndexContract{
        .name = "orders_customer",
        .create_sql = "create index orders(o_w_id, o_d_id, o_c_id, o_id);",
    },
    LoadIndexContract{
        .name = "order_line_pk",
        .create_sql = "create index order_line(ol_w_id, ol_d_id, ol_o_id, ol_number);",
    },
    LoadIndexContract{.name = "item_pk", .create_sql = "create index item(i_id);"},
    LoadIndexContract{
        .name = "stock_pk",
        .create_sql = "create index stock(s_w_id, s_i_id);",
    },
};

}  // namespace db::tpcc
