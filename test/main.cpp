#include "Card.hpp"
#include <iostream>

int main()
{
    card::Card card{"test.mc"};
    card.setGame("GM8E");
    card.setMaker("01");
    printf("Selected Game ID: %.4s\n", card.getGame());
    printf("Selected Maker ID: %.2s\n", card.getMaker());
    return 0;
}
